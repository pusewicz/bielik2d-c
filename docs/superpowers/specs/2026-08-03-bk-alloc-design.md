# bk_alloc — pluggable allocators

**Date:** 2026-08-03
**Status:** approved design, pre-implementation
**Module:** `include/bielik/bk_alloc.h`, `src/bk_alloc.c`, `src/internal/bk_alloc_internal.h`

## §0 Goals and decisions already made

Four goals, all confirmed as motivating: per-system memory budgets/tracking, arena/frame
performance, embedder control (a game supplies its own heap), and test injection
(counting/failing allocators). Decisions locked during design review:

- **Full retrofit**: every existing module routes through the new interface, not just new code.
- **Abort on OOM**: framework call sites never observe allocation failure; the wrapper logs
  and aborts. (One documented exception: the frame arena's existing drop-the-work contract.)
- **Third-party wiring**: SDL now; PhysFS/Box2D/stb when they arrive, each at its init point.
- **Architecture**: two-tier — a base allocator in `BK_AppDesc` plus optional per-object
  allocators in module desc structs (sokol/miniaudio shape), with per-module tag counters.
- **Callback shape**: three-function struct with context pointer, sized free, and sized
  realloc (old *and* new size). This scores 3/3 on Wellons' allocator-interface checklist
  ("So you want custom allocator support in your C library", 2023-12-17); a bare
  malloc/free pair without ctx cannot express an arena and is the documented anti-pattern.

Rejected alternatives, with the deciding evidence:

- **Zig-style explicit allocator parameter on every function** — reworks landed hot paths
  (`bk_gfx`/`bk_draw` depend on the global frame arena's pointer-stability contract) for
  viral signature churn users won't exercise. Even Odin, the implicit-context language,
  moved its OS layer to explicit params only at the *library boundary* — which is the desc
  struct here, not every call.
- **Vulkan-style per-call allocator** — `VkAllocationCallbacks` appears on 234 entry
  points; SDL's own Vulkan backend passes NULL at every one. Per-call granularity is a
  proven adoption failure.
- **Global override only (Cute Framework shape)** — CF's `CF_Allocator` has a `udata`
  field but only one global install path, so it can never route per-instance. That is the
  specific trap this design avoids.
- **Odin-style mode-enum single proc** — expressive (FREE_ALL, alignment) but more
  ceremony per implementation, and its introspection modes are acknowledged warts. The
  three-fn struct with both sizes present loses no tooling capability: a wrapper can
  classify every operation from its arguments.

## §1 Public header (complete)

```c
#pragma once
#include <bielik/bk_types.h>

// ---------------------------------------------------------------------------
// BK_Allocator — pluggable allocation for persistent (non-frame) memory.
//
// Not for per-frame scratch: that is bk_frame_alloc (bk_app.h), which also
// remains the only aligned-allocation path. alloc_fn must return memory
// suitably aligned for max_align_t, like malloc; alignment is deliberately
// not a parameter here, and adding one later is a recorded reversal.
// ---------------------------------------------------------------------------

/// A pluggable allocator. All-zero means: use the framework default (SDL heap).
/// If any function is set, all three must be set — partial override is invalid
/// and rejected wherever an allocator is installed (boot returns BK_FAIL;
/// object creation logs and fails). Sizes are signed (isize); all three
/// functions receive the ctx pointer last. free_fn receives the original
/// allocation size; realloc_fn receives both old and new size — the framework
/// always supplies them, so implementations never need per-allocation headers.
/// Returned memory is uninitialized (zeroing is the caller layer's job) and
/// must be aligned for max_align_t. Returning nullptr signals failure; the
/// framework treats that as fatal (logs and aborts). Thread-safety is the
/// implementation's concern: the base allocator (BK_AppDesc) must be
/// thread-safe if app callbacks allocate off the main thread; per-object
/// allocators are called only where their object is documented to live.
typedef struct BK_Allocator {
  void *(*alloc_fn)(isize size, void *ctx);
  void *(*realloc_fn)(void *ptr, isize old_size, isize new_size, void *ctx);
  void (*free_fn)(void *ptr, isize size, void *ctx);
  void *ctx;
} BK_Allocator;

/// Per-system accounting tags. Every framework allocation is counted under
/// exactly one tag, regardless of which allocator serves it.
typedef enum BK_MemTag {
  BK_MEM_TAG_APP,   // app core: boot, task system, misc
  BK_MEM_TAG_FRAME, // frame arena backing blocks
  BK_MEM_TAG_GFX,   // gfx object wrappers (buffers, textures, pipelines, ...)
  BK_MEM_TAG_DRAW,  // bk_draw persistent state (per-frame GPU buffer wrappers)
  BK_MEM_TAG_ATLAS, // atlas cache: records, hash, pixel staging, atlas images
  BK_MEM_TAG_SDL,   // SDL-internal allocations, when routed (see §5)
  BK_MEM_TAG_COUNT,
} BK_MemTag;

/// Live/lifetime counters for one tag. Reads are atomic per field, not
/// mutually consistent as a snapshot — fine for HUDs and leak asserts.
typedef struct BK_MemStats {
  isize live_bytes;   // currently allocated
  isize live_allocs;  // currently outstanding allocations
  isize peak_bytes;   // high-water mark of live_bytes
  isize total_allocs; // lifetime allocation count
} BK_MemStats;

/// Returns counters for one tag. Valid any time (zeroes before boot).
BK_MemStats bk_mem_stats(BK_MemTag tag);

/// Short lowercase name for a tag ("gfx", "atlas", ...), for HUDs and logs.
const char *bk_mem_tag_name(BK_MemTag tag);

// ---------------------------------------------------------------------------
// Allocation call layer. Macros so every call site captures __FILE__/__LINE__
// — consumed by the wrapper (OOM messages today, a debug site-table later),
// never passed to BK_Allocator implementations, which stay signature-minimal.
// a == nullptr (or all-zero *a) uses the framework default allocator.
// On failure: SDL_Log with "BK: " prefix, BK_ASSERT, then abort() — call
// sites never see nullptr.
// ---------------------------------------------------------------------------

void *bk__alloc_site(const BK_Allocator *a, isize size, bool zero, const char *file, int line);
void *bk__realloc_site(const BK_Allocator *a, void *ptr, isize old_size, isize new_size,
                       const char *file, int line);
void bk__free_site(const BK_Allocator *a, void *ptr, isize size);

/// Allocates size bytes (uninitialized / zeroed).
#define bk_alloc(a, size) bk__alloc_site((a), (size), false, __FILE__, __LINE__)
#define bk_alloc_zero(a, size) bk__alloc_site((a), (size), true, __FILE__, __LINE__)

/// Resizes ptr from old_size to new_size bytes. ptr may be nullptr iff old_size == 0.
#define bk_realloc(a, ptr, old_size, new_size)                                                     \
  bk__realloc_site((a), (ptr), (old_size), (new_size), __FILE__, __LINE__)

/// Frees ptr (size must equal the allocation's current size). nullptr is a no-op.
#define bk_free(a, ptr, size) bk__free_site((a), (ptr), (size))

/// Typed, zeroed single-object / array allocation.
#define BK_NEW(a, T) ((T *)bk_alloc_zero((a), (isize)sizeof(T)))
#define BK_NEW_ARRAY(a, T, n) ((T *)bk_alloc_zero((a), (isize)sizeof(T) * (n)))

// ---------------------------------------------------------------------------
// Helper allocators.
// ---------------------------------------------------------------------------

/// An allocator that BK_ASSERTs on any call. Install it where allocation is a
/// bug ("this path must not allocate") to turn a profiling question into a
/// testable assertion.
BK_Allocator bk_allocator_panic(void);

/// State for bk_allocator_counting. Counters are plain (not atomic): use from
/// one thread at a time (tests, HUD probes). inner all-zero means the default
/// heap serves the actual memory.
typedef struct BK_CountingAllocator {
  BK_Allocator inner;
  isize live_bytes;
  isize live_allocs;
  isize peak_bytes;
  isize total_allocs;
} BK_CountingAllocator;

/// Returns an allocator that forwards to state->inner and maintains the
/// counters. state must outlive every allocation made through the returned
/// allocator. Mismatched free/realloc sizes surface as nonzero live_bytes at
/// teardown — the leak-check idiom is: create object with this allocator,
/// destroy it, assert live_bytes == 0 && live_allocs == 0.
BK_Allocator bk_allocator_counting(BK_CountingAllocator *state);
```

Notes on the header:

- **SDL-free on purpose**, like `bk_types.h` — only `bk_types.h` is included, so any
  module (including SDL-free ones) can include it for the types.
- **Field names `alloc_fn`/`realloc_fn`/`free_fn`**: sokol shipped fields named
  `alloc`/`free` and had to make a breaking rename (sokol issue #903) because `free`
  collides with user macros when the C allocator is replaced. flecs hit the same wall
  (`free_`). Learn it for free.
- **`bk__*_site` are public-visible internal-prefixed functions** — same pattern as
  `bk__boot` in `bk_app.h`: callable because macros need them, `bk__`-prefixed because
  user code should write the macros.
- **Zero-by-default at the typed layer** (`BK_NEW`/`BK_NEW_ARRAY` zero; `bk_alloc` does
  not): Fleury and Wellons converged on zero-by-default with a named opt-out;
  implementations stay trivial because zeroing is the wrapper's job (sokol's
  `_sg_malloc_clear` pattern). The default allocator forwards `zero` to `SDL_calloc`
  rather than memset-after-malloc.

## §2 Internal seam

`src/internal/bk_alloc_internal.h` (declarations move out of `bk_app_internal.h`):

```c
/// Installs the app-wide base allocator; validates all-or-nothing, returns
/// false (and logs) on a partially-set struct. Called once from bk__boot
/// before SDL_Init; tests may call it directly. nullptr resets to default.
bool bk__alloc_install(const BK_Allocator *base);

/// Framework-internal allocation: like the public layer, plus a tag and an
/// allocator choice. a == nullptr means the installed base allocator.
void *bk__alloc(const BK_Allocator *a, BK_MemTag tag, isize size, bool zero, const char *file,
                int line);
void *bk__realloc(const BK_Allocator *a, BK_MemTag tag, void *ptr, isize old_size, isize new_size,
                  const char *file, int line);
void bk__free(const BK_Allocator *a, BK_MemTag tag, void *ptr, isize size);

// Call-site macros used by framework code (capture file/line):
#define BK__ALLOC(a, tag, size) bk__alloc((a), (tag), (size), false, __FILE__, __LINE__)
#define BK__ALLOC_ZERO(a, tag, size) bk__alloc((a), (tag), (size), true, __FILE__, __LINE__)
#define BK__REALLOC(a, tag, p, old, new) bk__realloc((a), (tag), (p), (old), (new), __FILE__, __LINE__)
#define BK__FREE(a, tag, p, size) bk__free((a), (tag), (p), (size))
```

- The existing `bk__alloc(usize)`/`bk__realloc`/`bk__free` signatures are replaced; all
  11 main-branch call sites update mechanically (each frees a fixed-size struct or a
  buffer whose size is in scope). The frame arena's chunk allocations use
  `BK_MEM_TAG_FRAME`; its public API and pointer-stability contract are untouched.
- **Tag counters are always-on** (budgets are a feature, not a debug mode), maintained in
  the seam with C23 `<stdatomic.h>` — checked SDL first per house rule: SDL3 has
  `SDL_AtomicInt`/`SDL_AtomicU32` but no 64-bit atomic add, and `isize` is 64-bit here.
- Counting happens **at the seam, per tag, regardless of which allocator serves the
  request** — a custom atlas allocator still shows up under `BK_MEM_TAG_ATLAS`.
- The public `bk_alloc()` layer (§1) does not touch tag counters; tags are framework
  accounting. Game code that wants counted allocations wraps with
  `bk_allocator_counting`.

## §3 App integration

`BK_AppDesc` gains one field (after `tasks`, same all-zero-means-default convention as
`BK_TaskSystemDesc`):

```c
BK_Allocator allocator; // base allocator for all framework allocation; all-zero => SDL heap
```

`bk_app.h` includes `<bielik/bk_alloc.h>`. `bk__boot` calls `bk__alloc_install` before
`SDL_Init`; a partial struct is a boot failure (`"BK: "` log + `BK_FAIL`), per the
no-silent-failure rule.

Embedder end-to-end:

```c
// nothing:
bk_run(&(BK_AppDesc){.window = {.title = "Game"}}, argc, argv);

// custom heap everywhere, SDL included:
bk_run(&(BK_AppDesc){
    .allocator = {.alloc_fn = heap_alloc, .realloc_fn = heap_realloc,
                  .free_fn = heap_free, .ctx = &heap},
}, argc, argv);

// debug HUD:
BK_MemStats gfx = bk_mem_stats(BK_MEM_TAG_GFX);
```

## §4 Module adoption

- **Atlas (the heavy consumer, retrofit now):** `BK_AtlasDesc` gains
  `BK_Allocator allocator;` (all-zero ⇒ base). `bk__atlas_create` validates it
  (all-or-nothing; log + nullptr on partial) and stores a resolved copy in `BK_Atlas` —
  an object that frees later must retain its allocator (miniaudio's rule). All 47 raw
  `SDL_malloc`-family sites become `BK__ALLOC`/`BK__REALLOC`/`BK__FREE` with
  `BK_MEM_TAG_ATLAS` and `&atlas->allocator`. Sized free needs sizes the teardown path
  doesn't currently hold (~7 frees in `bk__atlas_destroy`); the retrofit adds the missing
  capacity fields. This closes the standing PLAN §2 violation: afterwards
  `grep SDL_malloc src/` hits only `bk_alloc.c`'s default allocator.
- **gfx object wrappers** (buffer/texture/sampler/canvas/pipeline — 16–40-byte structs):
  base allocator via `BK_MEM_TAG_GFX`. No desc-struct changes now (three of the five
  creators have no desc at all); a per-object allocator for them is YAGNI until a use
  case shows up.
- **bk_draw:** allocates persistent memory only through the gfx creators (the two
  per-frame GPU-buffer wrappers), which count under `BK_MEM_TAG_GFX`; everything else it
  does is frame-arena (`BK_MEM_TAG_FRAME`). `BK_MEM_TAG_DRAW` therefore starts with zero
  traffic and exists for P3.5+ allocations made directly by the draw layer. If
  per-owner attribution of gfx objects is wanted later, the creators can grow a tag
  parameter — deferred.
- **Future modules (P3.5+, VFS, audio):** desc gains `BK_Allocator allocator` from day
  one; new tag per system.

## §5 Third-party wiring

**SDL (this effort).** Only when the embedder set a custom base allocator, `bk__boot`
calls `SDL_SetMemoryFunctions` before `SDL_Init`, adapting to SDL's signatures:

- SDL's function types carry no ctx and its `free` carries no size. The adapter
  over-allocates one `max_align_t`-sized header per SDL allocation to store the size,
  so it can call the base allocator's sized `free_fn`/`realloc_fn` honestly. Overhead:
  16 bytes per SDL-internal allocation, tagged `BK_MEM_TAG_SDL`.
- The adapter routes to the installed base allocator through the same file-static the
  seam uses (acceptable: the base allocator is inherently one-per-app).
- **Known risk, checked empirically during implementation:** SDL documents the swap as
  safe only before *any* SDL allocation, and the `SDL_MAIN_USE_CALLBACKS` entry path may
  allocate before `bk__boot` runs. If it does, the fallback is: don't install, log
  `"BK: SDL allocations predate boot; SDL not routed through the app allocator"`, and
  `BK_MEM_TAG_SDL` stays zero. An honest undercount beats a cross-allocator free.

**PhysFS/Box2D/stb (future phases, pattern reserved now):** each is wired at its init
point with the same header-shim technique where its interface lacks sizes
(PhysFS) and directly where it has them (Box2D's `b2FreeFcn` is already sized; stb_truetype
takes a userdata). Each gets its own tag. This is a per-phase checklist item, not a
retrofit.

## §6 OOM policy

- `bk__alloc`/`bk__realloc` (and the public layer): on nullptr from the implementation,
  `SDL_Log("BK: out of memory (%td bytes, %s) at %s:%d", ...)`, then `BK_ASSERT(false)`,
  then `abort()` — deterministic even when asserts are set to ignore. Call sites are
  written as if allocation cannot fail, which is what keeps them clean.
- The wrappers `BK_ASSERT(size >= 0)` (and `old_size`/`new_size` likewise): a negative
  size is a bug — e.g. an overflowed `BK_NEW_ARRAY` product — not an OOM, and signed
  sizes are what make the check expressible at all.
- Creation-path failures that are *not* allocator OOM (e.g. atlas desc validation) keep
  the existing convention: log + `BK_FAIL`/nullptr.
- **Exception (pre-existing, unchanged):** `bk_frame_alloc` keeps its shipped contract —
  assert, return nullptr, callers drop the frame's work and continue (see
  `DEVIATIONS.md` on the chunk-chain arena). A dropped batch is a glitch; a mid-frame
  abort is a crash. This asymmetry is deliberate and documented here and in
  `DEVIATIONS.md`.

## §7 Testing

Per the repo's mutation-verification discipline: for each test, break the code it names
and watch it fail before trusting green.

- `tests/test_alloc.c` (pico-style, no app boot — the `test_arena.c` model):
  install validation (partial struct rejected, all-zero accepted, nullptr resets),
  default-heap fallback, counting-allocator arithmetic across
  alloc/alloc_zero/realloc/free (live/peak/total), `BK_NEW` zeroing, per-tag stats via
  the internal seam, panic allocator (with SDL assert handler swapped to observe).
- OOM abort path: a tiny helper binary allocates through a failing allocator; the CTest
  entry asserts nonzero exit (the one behavior a same-process test can't observe).
- `tests/test_atlas.c`: existing scenarios re-run under `bk_allocator_counting`,
  asserting `live_bytes == 0 && live_allocs == 0` after `bk__atlas_destroy` — turns the
  existing 1456-line suite into a leak detector. A deliberately-unbalanced control case
  verifies the harness actually detects leaks (mutation discipline).
- A panic-allocator test pins bk_draw's contract: no persistent allocations in the
  per-frame path beyond the two known GPU-buffer wrappers.
- Failing-after-N allocator lives in test code, not the public header.

## §8 Documentation

- `DEVIATIONS.md` entries: (1) signed `isize` sizes vs SDL's `size_t` — casts at the
  boundary, signed-size rationale; (2) PLAN §2's "SDL_malloc wrappers so
  `SDL_SetMemoryFunctions` covers everything" is inverted — `BK_Allocator` is now the
  source of truth and SDL is wired into *it*; (3) frame-arena drop-work vs allocator
  abort — two OOM policies, each documented where it applies; (4) `<stdatomic.h>` where
  SDL has no 64-bit atomics.
- Doc comments per convention; `bk_alloc.h` standalone-compiles (existing enforcement).
- CLAUDE.md gains a line on the allocation rules once landed (post-implementation).

## §9 Implementation order

Five steps, each independently green:

1. `bk_alloc` module + `test_alloc.c` (pure addition).
2. Seam reroute: new internal signatures, tags, stats, `BK_AppDesc.allocator`,
   `bk__alloc_install` in boot; 11 sites updated.
3. Atlas retrofit: 47 sites, desc field, retained copy, capacity fields, leak harness.
4. SDL wiring: adapter + pre-boot allocation check + fallback.
5. Docs sweep: DEVIATIONS entries, header docs, CLAUDE.md line.

## §10 Out of scope

- Frame arena API changes (backing-buffer injection, usage query, double-buffering) —
  the current arena already serves the temp story; extending it is its own effort.
- A per-pointer tracking allocator (site table keyed by pointer, leak reports naming
  file:line) — the seam already captures file/line so this needs no signature changes;
  future work.
- Per-object allocators for gfx wrapper objects; a gfx-creator tag parameter.
- Scratch-arena pairs / Wellons-style by-value scratch — revisit when a module needs
  non-frame scratch memory.
