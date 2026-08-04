#define SDL_MAIN_HANDLED 1

#include "internal/bk_alloc_internal.h"
#include "internal/bk_app_internal.h"
#include "internal/bk_draw_internal.h"
#include "internal/bk_gfx_internal.h"
#include "internal/bk_task_internal.h"

#include <bielik/bk_app.h>
#include <bielik/bk_time.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#define BK_STR_(x) #x
#define BK_STR(x)  BK_STR_(x)

static_assert(BK_CONTINUE == (BK_Result)SDL_APP_CONTINUE, "BK_Result must mirror SDL_AppResult");
static_assert(BK_DONE == (BK_Result)SDL_APP_SUCCESS, "BK_Result must mirror SDL_AppResult");
static_assert(BK_FAIL == (BK_Result)SDL_APP_FAILURE, "BK_Result must mirror SDL_AppResult");

const char *bk_version_string(void) {
  return BK_STR(BK_VERSION_MAJOR) "." BK_STR(BK_VERSION_MINOR) "." BK_STR(BK_VERSION_PATCH);
}

static constexpr usize s_arena_default_capacity = 4 * 1024 * 1024;

/// One backing block of the frame arena. The arena is a chain of these rather than a
/// single block precisely so that running out of room never moves memory already handed
/// out: growth appends a chunk, it never reallocates an existing one. Consumers hold
/// arena pointers across later bk_frame_alloc calls (bk_gfx's and bk_draw's per-frame
/// record chains both write through the previous node), so pointer stability for the
/// whole frame is a contract, not a convenience -- see bk_frame_alloc in bk_app.h.
typedef struct BK_ArenaChunk {
  struct BK_ArenaChunk *next;
  unsigned char *base;
  usize capacity;
  usize used;
} BK_ArenaChunk;

static struct {
  BK_ArenaChunk *first;
  BK_ArenaChunk *current; // where the next allocation is tried; nullptr before the first
} s_frame_arena;

/// Carves size bytes at align out of chunk, or returns nullptr if they do not fit. The
/// capacity test is written as a subtraction so a huge size cannot wrap the addition.
static void *s_chunk_alloc(BK_ArenaChunk *chunk, usize size, usize align) {
  uintptr_t base_addr = (uintptr_t)chunk->base;
  uintptr_t aligned_addr = (base_addr + chunk->used + (align - 1)) & ~(uintptr_t)(align - 1);
  usize offset = (usize)(aligned_addr - base_addr);
  if (offset > chunk->capacity || size > chunk->capacity - offset) {
    return nullptr;
  }
  chunk->used = offset + size;
  return chunk->base + offset;
}

/// Allocates a chunk with the given capacity; allocation failure aborts (BK__ALLOC's
/// OOM contract). The header is a separate allocation from the payload so the payload
/// keeps the allocator's own max_align_t guarantee rather than whatever offset a
/// trailing header would land on.
static BK_ArenaChunk *s_chunk_create(usize capacity) {
  BK_ArenaChunk *chunk = BK__ALLOC(nullptr, BK_MEM_TAG_FRAME, (isize)sizeof *chunk);
  unsigned char *base = BK__ALLOC(nullptr, BK_MEM_TAG_FRAME, (isize)capacity);
  *chunk = (BK_ArenaChunk){.base = base, .capacity = capacity};
  return chunk;
}

void bk__arena_reset(void) {
  // Every chunk is recycled, not freed: a frame that needed two chunks last time gets
  // both back with no allocator traffic, and the first chunk's address is stable across
  // frames.
  for (BK_ArenaChunk *chunk = s_frame_arena.first; chunk != nullptr; chunk = chunk->next) {
    chunk->used = 0;
  }
  s_frame_arena.current = s_frame_arena.first;
}

void bk__arena_free(void) {
  BK_ArenaChunk *chunk = s_frame_arena.first;
  while (chunk != nullptr) {
    BK_ArenaChunk *next = chunk->next;
    BK__FREE(nullptr, BK_MEM_TAG_FRAME, chunk->base, (isize)chunk->capacity);
    BK__FREE(nullptr, BK_MEM_TAG_FRAME, chunk, (isize)sizeof *chunk);
    chunk = next;
  }
  s_frame_arena.first = nullptr;
  s_frame_arena.current = nullptr;
}

void *bk_frame_alloc(usize size, usize align) {
  if (align == 0) {
    align = alignof(max_align_t);
  } else {
    BK_ASSERT((align & (align - 1)) == 0);
  }

  // Fast path: the chunk allocations are already coming from.
  if (s_frame_arena.current != nullptr) {
    void *ptr = s_chunk_alloc(s_frame_arena.current, size, align);
    if (ptr != nullptr) {
      return ptr;
    }
    // Full. Chunks recycled by bk__arena_reset sit further down the chain, in
    // non-decreasing capacity order; take the first that fits. Skipping past a chunk
    // strands its remaining bytes for the rest of the frame, which is the usual arena
    // trade and far cheaper than the pointer invalidation the alternative would cost.
    for (BK_ArenaChunk *chunk = s_frame_arena.current->next; chunk != nullptr;
         chunk = chunk->next) {
      ptr = s_chunk_alloc(chunk, size, align);
      if (ptr != nullptr) {
        s_frame_arena.current = chunk;
        return ptr;
      }
    }
  }

  // Nothing recycled fits (or there is no arena yet): append a fresh chunk, doubling the
  // last one's capacity until the request fits so a big allocation gets a big chunk.
  BK_ArenaChunk *tail = s_frame_arena.current;
  while (tail != nullptr && tail->next != nullptr) {
    tail = tail->next;
  }
  usize capacity = tail != nullptr ? tail->capacity : s_arena_default_capacity;
  usize worst_case = size + (align - 1);
  while (capacity < worst_case) {
    capacity *= 2;
  }

  BK_ArenaChunk *chunk = s_chunk_create(capacity);
  BK_ASSERT(chunk != nullptr);
  if (chunk == nullptr) {
    return nullptr;
  }
  if (tail != nullptr) {
    tail->next = chunk;
  } else {
    s_frame_arena.first = chunk;
  }
  s_frame_arena.current = chunk;
  SDL_Log("BK: frame arena added a %zu byte chunk", capacity);

  void *ptr = s_chunk_alloc(chunk, size, align);
  BK_ASSERT(ptr != nullptr); // the chunk was sized for this request; only a wrapped
                             // size + align - 1 can miss
  return ptr;
}

#ifndef NDEBUG
static constexpr bool s_gpu_debug_mode = true;
#else
static constexpr bool s_gpu_debug_mode = false;
#endif

typedef struct BK_AppState {
  BK_AppDesc desc;
  BK_Clock clock;
  SDL_Window *window;
  SDL_GPUDevice *gpu;
  u64 boot_now_ns;
  i32 window_w;
  i32 window_h;
  bool booted; // true once bk__boot reaches a state desc.init had a chance to populate
} BK_AppState;

static BK_AppState s_app;

// Re-queries the window's drawable size in pixels and clamps to >= 1 (a 0-sized
// drawable during minimize would otherwise propagate into GPU texture creation
// downstream). Called once at boot and again on every SDL_EVENT_WINDOW_RESIZED/
// SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED -- idempotent and cheap enough to run on
// either event regardless of which one a given platform actually fires.
static void s_refresh_window_size(void) {
  i32 w = 0, h = 0;
  SDL_GetWindowSizeInPixels(s_app.window, &w, &h);
  s_app.window_w = w > 0 ? w : 1;
  s_app.window_h = h > 0 ? h : 1;
}

static SDL_AppResult s_boot_fail(const char *msg) {
  SDL_Log("BK: %s", msg);
#ifdef NDEBUG
  // Release builds surface boot failures in a dialog so a double-clicked game explains
  // itself instead of silently vanishing. That dialog is modal, though, so anything
  // with no user to dismiss it -- tests, CI, headless runs -- hangs until it's killed.
  // BK_HINT_NO_ERROR_DIALOG opts out; it resolves from the environment as well as from
  // SDL_SetHint, so CI can set it without a code change.
  if (!SDL_GetHintBoolean(BK_HINT_NO_ERROR_DIALOG, false)) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Bielik2D", msg, s_app.window);
  }
#endif
  return SDL_APP_FAILURE;
}

SDL_AppResult bk__boot(BK_AppDesc (*get_desc)(void), void **appstate, int argc, char **argv) {
  s_app.desc = get_desc();

  if (s_app.desc.window.title == nullptr) {
    s_app.desc.window.title = "Bielik2D";
  }
  if (s_app.desc.window.width == 0) {
    s_app.desc.window.width = 1280;
  }
  if (s_app.desc.window.height == 0) {
    s_app.desc.window.height = 720;
  }
  if (s_app.desc.time.max_ticks_per_frame == 0) {
    s_app.desc.time.max_ticks_per_frame = 8;
  }
  if (s_app.desc.time.max_frame_dt == 0.0) {
    s_app.desc.time.max_frame_dt = 0.25;
  }

  if (!bk__alloc_install(&s_app.desc.allocator)) {
    return s_boot_fail("BK_AppDesc.allocator is partially set (all three functions or none)");
  }
  bk__alloc_route_sdl(); // best-effort; skipped cases are logged

  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD)) {
    char msg[256];
    SDL_snprintf(msg, sizeof msg, "SDL_Init failed: %s", SDL_GetError());
    return s_boot_fail(msg);
  }

  SDL_WindowFlags window_flags = SDL_WINDOW_HIDDEN;
  if (s_app.desc.window.resizable) {
    window_flags |= SDL_WINDOW_RESIZABLE;
  }
  if (s_app.desc.window.fullscreen) {
    window_flags |= SDL_WINDOW_FULLSCREEN;
  }
  s_app.window = SDL_CreateWindow(s_app.desc.window.title, s_app.desc.window.width,
                                  s_app.desc.window.height, window_flags);
  if (!s_app.window) {
    char msg[256];
    SDL_snprintf(msg, sizeof msg, "SDL_CreateWindow failed: %s", SDL_GetError());
    return s_boot_fail(msg);
  }
  s_refresh_window_size();

  s_app.gpu = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL |
                                      SDL_GPU_SHADERFORMAT_MSL,
                                  s_gpu_debug_mode, nullptr);
  if (!s_app.gpu) {
    char msg[256];
    SDL_snprintf(msg, sizeof msg, "SDL_CreateGPUDevice failed: %s", SDL_GetError());
    return s_boot_fail(msg);
  }

  if (!SDL_ClaimWindowForGPUDevice(s_app.gpu, s_app.window)) {
    char msg[256];
    SDL_snprintf(msg, sizeof msg, "SDL_ClaimWindowForGPUDevice failed: %s", SDL_GetError());
    return s_boot_fail(msg);
  }

  SDL_GPUPresentMode present_mode = SDL_GPU_PRESENTMODE_VSYNC;
  if (!s_app.desc.window.vsync &&
      SDL_WindowSupportsGPUPresentMode(s_app.gpu, s_app.window, SDL_GPU_PRESENTMODE_IMMEDIATE)) {
    present_mode = SDL_GPU_PRESENTMODE_IMMEDIATE;
  }
  if (!SDL_SetGPUSwapchainParameters(s_app.gpu, s_app.window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
                                     present_mode)) {
    char msg[256];
    SDL_snprintf(msg, sizeof msg, "SDL_SetGPUSwapchainParameters failed: %s", SDL_GetError());
    return s_boot_fail(msg);
  }

  bk__gfx_configure_swapchain_depth(s_app.desc.window.depth_stencil);
  bk__draw_init();

  SDL_ShowWindow(s_app.window);

  s_app.boot_now_ns = SDL_GetTicksNS();
  bk_clock_init(&s_app.clock, s_app.desc.time.tick_hz, s_app.desc.time.max_ticks_per_frame,
                s_app.desc.time.max_frame_dt, s_app.boot_now_ns);

  bk__task_set_desc(&s_app.desc.tasks);

  *appstate = s_app.desc.userdata;
  BK_Result result = BK_CONTINUE;
  if (s_app.desc.init) {
    result = s_app.desc.init(appstate, argc, argv);
  }
  if (result == BK_DONE) {
    s_app.booted = true;
    return (SDL_AppResult)BK_DONE;
  }
  if (result != BK_CONTINUE) {
    return s_boot_fail("app init callback failed");
  }

  s_app.booted = true;
  return SDL_APP_CONTINUE;
}

SDL_AppResult bk__iterate(void *appstate) {
  u64 now = SDL_GetTicksNS();
  BK_ClockFrame cf = bk_clock_advance(&s_app.clock, now);

  BK_FrameInfo info = {0};
  for (int i = 0; i < cf.ticks; i++) {
    // TODO(phase4): input snapshot slot — bk_input__begin_tick() runs here
    u64 tick_i = s_app.clock.tick - (u64)cf.ticks + (u64)i + 1;
    info = (BK_FrameInfo){
        .tick = tick_i,
        .sim_time = (f64)tick_i * bk_clock_fixed_dt(&s_app.clock),
        .real_time = (f64)(now - s_app.boot_now_ns) / 1e9,
        .dt = s_app.desc.time.tick_hz != 0 ? bk_clock_fixed_dt(&s_app.clock) : cf.frame_dt,
        .alpha = 0.0,
    };

    if (s_app.desc.update) {
      BK_Result result = s_app.desc.update(appstate, &info);
      if (result != BK_CONTINUE) {
        return (SDL_AppResult)result;
      }
    }
    // TODO(phase8): physics slot — bk_physics__step() runs here iff bk_physics_init was called
    if (s_app.desc.post_update) {
      BK_Result result = s_app.desc.post_update(appstate, &info);
      if (result != BK_CONTINUE) {
        return (SDL_AppResult)result;
      }
    }
  }

  info.tick = s_app.clock.tick;
  info.sim_time = bk_clock_sim_time(&s_app.clock);
  info.real_time = (f64)(now - s_app.boot_now_ns) / 1e9;
  info.dt = cf.frame_dt;
  info.alpha = cf.alpha;

  if (s_app.desc.render) {
    s_app.desc.render(appstate, &info);
  }
  bk__draw_collate();
  bk__gfx_flush();
  bk__arena_reset();

  return SDL_APP_CONTINUE;
}

SDL_AppResult bk__event(void *appstate, SDL_Event *event) {
  // Runs before any app .event handler (and isn't skipped by one returning early)
  // so bk_window_size is always current by the time app code can observe a resize.
  // Handles both events, idempotently: which one a given platform actually fires
  // for a given resize isn't guaranteed to be just one of the two.
  if (event->type == SDL_EVENT_WINDOW_RESIZED ||
      event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
    s_refresh_window_size();
  }

  if (s_app.desc.event) {
    BK_Result result = s_app.desc.event(appstate, event);
    return (SDL_AppResult)result;
  }
  if (event->type == SDL_EVENT_QUIT) {
    return SDL_APP_SUCCESS;
  }
  return SDL_APP_CONTINUE;
}

void bk__shutdown(void *appstate, SDL_AppResult result) {
  // Skip quit if boot never reached the point where desc.init could populate
  // *appstate -- e.g. init itself returned BK_FAIL, or an earlier boot step
  // (SDL_Init, window/GPU creation) failed. appstate is otherwise whatever
  // it was left at (typically nullptr), which every sample's quit callback
  // unconditionally dereferences.
  if (s_app.booted && s_app.desc.quit) {
    s_app.desc.quit(appstate, (BK_Result)result);
  }
  bk__arena_free();
  bk__draw_shutdown(); // must run before SDL_DestroyGPUDevice below (owns GPU resources)
  bk__gfx_shutdown();  // must run before SDL_DestroyGPUDevice below
  if (s_app.gpu && s_app.window) {
    SDL_ReleaseWindowFromGPUDevice(s_app.gpu, s_app.window);
  }
  if (s_app.gpu) {
    SDL_DestroyGPUDevice(s_app.gpu);
  }
  if (s_app.window) {
    SDL_DestroyWindow(s_app.window);
  }
  // Reset to the pristine state bk__boot expects on entry. s_app is a static, so a
  // second bk_run in the same process (every multi-scenario test binary) would otherwise
  // start its boot looking at this run's already-destroyed gpu/window -- s_boot_fail's
  // dialog path and a would-be second bk__shutdown both dereference/destroy through them.
  s_app.gpu = nullptr;
  s_app.window = nullptr;
  s_app.booted = false;
}

SDL_Window *bk_window(void) {
  return s_app.window;
}

SDL_GPUDevice *bk_gpu(void) {
  return s_app.gpu;
}

void bk_window_size(i32 *out_w, i32 *out_h) {
  if (out_w != nullptr) {
    *out_w = s_app.window_w;
  }
  if (out_h != nullptr) {
    *out_h = s_app.window_h;
  }
}

void bk_quit(void) {
  SDL_Event event = {.type = SDL_EVENT_QUIT};
  SDL_PushEvent(&event);
}

static BK_AppDesc s_run_desc;

static BK_AppDesc s_run_get_desc(void) {
  return s_run_desc;
}

static SDL_AppResult s_run_init(void **appstate, int argc, char **argv) {
  return bk__boot(s_run_get_desc, appstate, argc, argv);
}

static SDL_AppResult s_run_iterate(void *appstate) {
  return bk__iterate(appstate);
}

static SDL_AppResult s_run_event(void *appstate, SDL_Event *event) {
  return bk__event(appstate, event);
}

static void s_run_quit(void *appstate, SDL_AppResult result) {
  bk__shutdown(appstate, result);
}

int bk_run(const BK_AppDesc *desc, int argc, char **argv) {
  BK_ASSERT(desc != nullptr);

  s_run_desc = *desc;
  return SDL_EnterAppMainCallbacks(argc, argv, s_run_init, s_run_iterate, s_run_event, s_run_quit);
}
