#include "bk_test.h"
#include "internal/bk_atlas_internal.h"

#include <bielik/bk_alloc.h>

#include <SDL3/SDL.h>

#include <stdio.h>

// The cache never touches a GPU: it asks these four callbacks for pixels and texture
// handles. That is what makes every test here a required CI check on every platform.
//
// create_texture keeps a copy of the pixels it was handed. That copy is the whole point
// of the fake -- it is how a test can assert that the rect an entry reports actually
// contains that image's pixels, rather than only that some rect was reported.

constexpr i32 FAKE_MAX_TEXTURES = 64;
constexpr i32 FAKE_MAX_LOG = 512;

// Every test below builds its BK_AtlasDesc through s_make_desc, which routes the atlas's
// allocator through this counter. The end-of-main assertions turn every test's
// create/use/destroy cycle into a leak check for bk_atlas.c.
static BK_CountingAllocator s_atlas_counter;

typedef struct FakeTexture {
  u64 id;
  i32 width, height;
  u8 *pixels; // owned copy of what create_texture received
  bool alive;
} FakeTexture;

// The whole entry, not just its ids: what the cache *reported* is what tests need to
// assert against, and a log that kept only the texture id could not tell a correct rect
// from a plausible one.
typedef struct FakeLogEntry {
  BK_AtlasEntry entry;
  i32 batch_index;
} FakeLogEntry;

typedef struct FakeGpu {
  FakeTexture textures[FAKE_MAX_TEXTURES];
  i32 texture_count;
  u64 next_id;

  FakeLogEntry log[FAKE_MAX_LOG];
  i32 log_count;
  i32 batch_count;

  i32 get_pixels_calls;
  i32 create_calls;
  i32 destroy_calls;

  // Fault injection, used from Task 2 onward. Defaults mean "never fail".
  bool fail_get_pixels;
  u64 fail_get_pixels_image; // only meaningful when fail_get_pixels is true
  bool fail_get_pixels_all;  // every image, regardless of id -- see Task 4's step 6 test
  bool fail_create;
  i32 fail_create_on_call; // 1-based ordinal of the create_texture call that returns 0
} FakeGpu;

// A per-pixel pattern that identifies its source image and its position within it, so a
// test can tell "the right image landed in the wrong place" from "the wrong image landed
// in the right place".
static u8 s_pattern_byte(u64 image_id, i32 x, i32 y, i32 channel) {
  u64 mixed = image_id * 37u + (u64)(x * 5 + y * 11 + channel * 3);
  return (u8)(mixed & 0xFFu);
}

static bool fake_get_pixels(u64 image_id, void *buffer, i32 size, i32 width, i32 height,
                            void *udata) {
  FakeGpu *gpu = udata;
  gpu->get_pixels_calls++;
  if (gpu->fail_get_pixels_all ||
      (gpu->fail_get_pixels && image_id == gpu->fail_get_pixels_image)) {
    return false;
  }
  // The cache promises these agree; a mismatch here is a cache bug, not a fake bug.
  REQUIRE(size == width * height * 4);
  u8 *out = buffer;
  for (i32 y = 0; y < height; y++) {
    for (i32 x = 0; x < width; x++) {
      for (i32 channel = 0; channel < 4; channel++) {
        out[(y * width + x) * 4 + channel] = s_pattern_byte(image_id, x, y, channel);
      }
    }
  }
  return true;
}

static u64 fake_create_texture(const void *pixels, i32 width, i32 height, void *udata) {
  FakeGpu *gpu = udata;
  gpu->create_calls++;
  if (gpu->fail_create && gpu->create_calls == gpu->fail_create_on_call) {
    return 0;
  }
  REQUIRE(gpu->texture_count < FAKE_MAX_TEXTURES);
  FakeTexture *tex = &gpu->textures[gpu->texture_count++];
  tex->id = ++gpu->next_id;
  tex->width = width;
  tex->height = height;
  tex->alive = true;
  usize bytes = (usize)width * (usize)height * 4u;
  tex->pixels = SDL_malloc(bytes);
  REQUIRE(tex->pixels != nullptr);
  SDL_memcpy(tex->pixels, pixels, bytes);
  return tex->id;
}

static FakeTexture *s_find_texture(FakeGpu *gpu, u64 texture_id) {
  for (i32 i = 0; i < gpu->texture_count; i++) {
    if (gpu->textures[i].id == texture_id) {
      return &gpu->textures[i];
    }
  }
  return nullptr;
}

static void fake_destroy_texture(u64 texture_id, void *udata) {
  FakeGpu *gpu = udata;
  gpu->destroy_calls++;
  FakeTexture *tex = s_find_texture(gpu, texture_id);
  REQUIRE(tex != nullptr); // destroying a handle we never minted is a cache bug
  REQUIRE(tex->alive);     // double destroy is a cache bug
  tex->alive = false;
}

static void fake_submit_batch(const BK_AtlasEntry *entries, i32 count, void *udata) {
  FakeGpu *gpu = udata;
  REQUIRE(count > 0); // an empty batch is a cache bug
  for (i32 i = 0; i < count; i++) {
    REQUIRE(entries[i].texture_id == entries[0].texture_id); // one texture per batch
    REQUIRE(entries[i].texture_id != 0);                     // spec section 3.2
    REQUIRE(gpu->log_count < FAKE_MAX_LOG);
    gpu->log[gpu->log_count++] = (FakeLogEntry){
        .entry = entries[i],
        .batch_index = gpu->batch_count,
    };
  }
  gpu->batch_count++;
}

static BK_AtlasDesc s_make_desc(FakeGpu *gpu) {
  return (BK_AtlasDesc){
      .get_pixels = fake_get_pixels,
      .create_texture = fake_create_texture,
      .destroy_texture = fake_destroy_texture,
      .submit_batch = fake_submit_batch,
      .udata = gpu,
      .allocator = bk_allocator_counting(&s_atlas_counter),
  };
}

static void s_free_gpu(FakeGpu *gpu) {
  for (i32 i = 0; i < gpu->texture_count; i++) {
    SDL_free(gpu->textures[i].pixels);
  }
}

// Finds the log entry recorded for a given udata, or nullptr. Tests assert by udata, never
// by batch count alone -- a fake that mints monotonic texture ids makes "two batches" pass
// under a bug that gives every entry its own texture (design spec section 5.1).
static const FakeLogEntry *s_find_log(FakeGpu *gpu, u64 udata) {
  for (i32 i = 0; i < gpu->log_count; i++) {
    if (gpu->log[i].entry.udata == udata) {
      return &gpu->log[i];
    }
  }
  return nullptr;
}

// Counts SDL_Log lines that mention a watched image_id, so a test can assert on
// s_log_image_failure's own dedup behavior -- something none of the FakeGpu counters can
// see, since the log call is entirely separate from get_pixels/create_texture.
typedef struct LogCapture {
  u64 watch_image_id;
  i32 count;
} LogCapture;

static void SDLCALL s_capture_log(void *userdata, int category, SDL_LogPriority priority,
                                  const char *message) {
  (void)category;
  (void)priority;
  LogCapture *capture = userdata;
  char needle[32];
  SDL_snprintf(needle, sizeof(needle), "image %llu:", (unsigned long long)capture->watch_image_id);
  if (SDL_strstr(message, needle) != nullptr) {
    capture->count++;
  }
}

static void s_push(BK_Atlas *atlas, u64 image_id, i32 width, i32 height, u64 udata) {
  bk__atlas_push(atlas, (BK_AtlasEntry){
                            .image_id = image_id,
                            .udata = udata,
                            .width = width,
                            .height = height,
                        });
}

static void test_flush_makes_pushed_images_resident(void) {
  FakeGpu gpu = {0};
  BK_AtlasDesc desc = s_make_desc(&gpu);
  BK_Atlas *atlas = bk__atlas_create(&desc);
  REQUIRE(atlas != nullptr);

  s_push(atlas, 100, 8, 4, 1);
  // Nothing may happen before flush: push buffers, it does not upload (spec section 3).
  REQUIRE(gpu.get_pixels_calls == 0);
  REQUIRE(gpu.create_calls == 0);

  REQUIRE(bk__atlas_flush(atlas));
  REQUIRE(gpu.get_pixels_calls == 1);
  REQUIRE(gpu.create_calls == 1);
  REQUIRE(gpu.batch_count == 1);
  REQUIRE(gpu.log_count == 1);

  const FakeLogEntry *logged = s_find_log(&gpu, 1);
  REQUIRE(logged != nullptr);
  REQUIRE(logged->entry.texture_id != 0);

  bk__atlas_destroy(atlas);
  s_free_gpu(&gpu);
}

// Asserts that entry's reported rect, read out of the texture the fake recorded, holds
// exactly the pattern get_pixels wrote for that image. This is the assertion that catches
// a packing offset, a row-stride slip, or a y-flip -- none of which a rect-coordinate
// comparison would notice. Task 4 leans on it hardest.
static void s_require_rect_matches(FakeGpu *gpu, const BK_AtlasEntry *entry) {
  FakeTexture *tex = s_find_texture(gpu, entry->texture_id);
  REQUIRE(tex != nullptr);
  REQUIRE(tex->alive);
  REQUIRE(entry->max_x - entry->min_x == entry->width);
  REQUIRE(entry->max_y - entry->min_y == entry->height);
  REQUIRE(entry->min_x >= 0 && entry->max_x <= tex->width);
  REQUIRE(entry->min_y >= 0 && entry->max_y <= tex->height);
  for (i32 y = 0; y < entry->height; y++) {
    for (i32 x = 0; x < entry->width; x++) {
      for (i32 channel = 0; channel < 4; channel++) {
        i32 offset = ((entry->min_y + y) * tex->width + (entry->min_x + x)) * 4 + channel;
        REQUIRE(tex->pixels[offset] == s_pattern_byte(entry->image_id, x, y, channel));
      }
    }
  }
}

static void test_lonely_image_reports_its_whole_texture(void) {
  FakeGpu gpu = {0};
  BK_AtlasDesc desc = s_make_desc(&gpu);
  BK_Atlas *atlas = bk__atlas_create(&desc);
  REQUIRE(atlas != nullptr);

  s_push(atlas, 7, 8, 4, 42);
  REQUIRE(bk__atlas_flush(atlas));

  // One texture, sized exactly to the image.
  REQUIRE(gpu.texture_count == 1);
  REQUIRE(gpu.textures[0].width == 8);
  REQUIRE(gpu.textures[0].height == 4);

  // Assert against what the cache actually reported, not against a rect reconstructed
  // here -- a reconstructed rect would pass no matter what the cache wrote.
  const FakeLogEntry *logged = s_find_log(&gpu, 42);
  REQUIRE(logged != nullptr);
  REQUIRE(logged->entry.image_id == 7);
  REQUIRE(logged->entry.min_x == 0 && logged->entry.min_y == 0);
  REQUIRE(logged->entry.max_x == 8 && logged->entry.max_y == 4);
  s_require_rect_matches(&gpu, &logged->entry);

  bk__atlas_destroy(atlas);
  s_free_gpu(&gpu);
}

static void test_repeated_pushes_reuse_one_texture(void) {
  FakeGpu gpu = {0};
  BK_AtlasDesc desc = s_make_desc(&gpu);
  BK_Atlas *atlas = bk__atlas_create(&desc);
  REQUIRE(atlas != nullptr);

  s_push(atlas, 7, 8, 4, 1);
  REQUIRE(bk__atlas_flush(atlas));
  s_push(atlas, 7, 8, 4, 2);
  REQUIRE(bk__atlas_flush(atlas));

  // Residency is the point: the second frame must not re-fetch or re-upload.
  REQUIRE(gpu.get_pixels_calls == 1);
  REQUIRE(gpu.create_calls == 1);
  REQUIRE(gpu.batch_count == 2);

  bk__atlas_destroy(atlas);
  s_free_gpu(&gpu);
}

static void test_distinct_images_batch_separately_in_push_order(void) {
  FakeGpu gpu = {0};
  BK_AtlasDesc desc = s_make_desc(&gpu);
  BK_Atlas *atlas = bk__atlas_create(&desc);
  REQUIRE(atlas != nullptr);

  // Three images, each on its own lonely texture, interleaved so that grouping by texture
  // genuinely has to move entries: pushed A B A C B, expected batches [A A] [B B] [C].
  s_push(atlas, 10, 4, 4, 100);
  s_push(atlas, 20, 4, 4, 200);
  s_push(atlas, 10, 4, 4, 101);
  s_push(atlas, 30, 4, 4, 300);
  s_push(atlas, 20, 4, 4, 201);
  REQUIRE(bk__atlas_flush(atlas));

  REQUIRE(gpu.batch_count == 3);
  REQUIRE(gpu.log_count == 5);

  // Batches arrive ordered by their earliest-pushed entry, not by texture handle value.
  REQUIRE(s_find_log(&gpu, 100)->batch_index == 0);
  REQUIRE(s_find_log(&gpu, 101)->batch_index == 0);
  REQUIRE(s_find_log(&gpu, 200)->batch_index == 1);
  REQUIRE(s_find_log(&gpu, 201)->batch_index == 1);
  REQUIRE(s_find_log(&gpu, 300)->batch_index == 2);

  // Within a batch, push order survives. The log is append-order, so 100 must precede 101.
  // The sequence is deliberately not palindromic: a reversal must map it somewhere else,
  // or an anti-stable sort passes this test (spec section 4.2).
  REQUIRE(gpu.log[0].entry.udata == 100);
  REQUIRE(gpu.log[1].entry.udata == 101);
  REQUIRE(gpu.log[2].entry.udata == 200);
  REQUIRE(gpu.log[3].entry.udata == 201);
  REQUIRE(gpu.log[4].entry.udata == 300);

  bk__atlas_destroy(atlas);
  s_free_gpu(&gpu);
}

// Distinguishes "batches ordered by first-seen entry" from "batches ordered by ascending
// texture_id" -- two policies that agree whenever every texture in a flush is brand new,
// because handles are then minted in exactly first-seen order. Frame 1 mints handles in
// push order (40, 50, 60 -> 1, 2, 3). Frame 2 pushes the same images in reverse, so every
// image is already resident and nothing is created -- push order and ascending-handle
// order now disagree, and only push order is correct (spec section 4.2).
static void test_batch_order_follows_push_order_not_texture_handle_order(void) {
  FakeGpu gpu = {0};
  BK_AtlasDesc desc = s_make_desc(&gpu);
  BK_Atlas *atlas = bk__atlas_create(&desc);
  REQUIRE(atlas != nullptr);

  s_push(atlas, 40, 4, 4, 401);
  s_push(atlas, 50, 4, 4, 501);
  s_push(atlas, 60, 4, 4, 601);
  REQUIRE(bk__atlas_flush(atlas));
  REQUIRE(gpu.batch_count == 3); // handles: 40 -> 1, 50 -> 2, 60 -> 3

  s_push(atlas, 60, 4, 4, 602);
  s_push(atlas, 50, 4, 4, 502);
  s_push(atlas, 40, 4, 4, 402);
  REQUIRE(bk__atlas_flush(atlas));

  // batch_count is not reset between flushes, so frame 2's three batches are indices 3-5.
  REQUIRE(gpu.batch_count == 6);
  REQUIRE(s_find_log(&gpu, 602)->batch_index == 3);
  REQUIRE(s_find_log(&gpu, 502)->batch_index == 4);
  REQUIRE(s_find_log(&gpu, 402)->batch_index == 5);

  bk__atlas_destroy(atlas);
  s_free_gpu(&gpu);
}

static void test_flush_drains_the_push_buffer(void) {
  FakeGpu gpu = {0};
  BK_AtlasDesc desc = s_make_desc(&gpu);
  BK_Atlas *atlas = bk__atlas_create(&desc);
  REQUIRE(atlas != nullptr);

  s_push(atlas, 5, 4, 4, 1);
  REQUIRE(bk__atlas_flush(atlas));
  REQUIRE(gpu.batch_count == 1);

  // A second flush with nothing pushed reports nothing. If the buffer were not drained,
  // last frame's entry would be drawn again.
  REQUIRE(bk__atlas_flush(atlas));
  REQUIRE(gpu.batch_count == 1);
  REQUIRE(gpu.log_count == 1);

  bk__atlas_destroy(atlas);
  s_free_gpu(&gpu);
}

static void test_many_images_survive_index_growth(void) {
  FakeGpu gpu = {0};
  BK_AtlasDesc desc = s_make_desc(&gpu);
  BK_Atlas *atlas = bk__atlas_create(&desc);
  REQUIRE(atlas != nullptr);

  // Enough distinct ids to force the residency index to grow and rehash several times,
  // with ids spaced so a weak hash collides. Every one must still be found on re-push.
  //
  // i == 0 gives image_id 0, on purpose: the index marks empty slots in its value array,
  // not with a reserved key, so 0 is an ordinary id (spec section 4.3). An implementation
  // that used a 0 key as its empty marker would lose exactly this image.
  constexpr i32 IMAGE_COUNT = 50;
  for (i32 i = 0; i < IMAGE_COUNT; i++) {
    s_push(atlas, (u64)i * 64u, 2, 2, (u64)i);
  }
  REQUIRE(bk__atlas_flush(atlas));
  REQUIRE(gpu.create_calls == IMAGE_COUNT);
  REQUIRE(s_find_log(&gpu, 0) != nullptr); // image_id 0 arrived like any other

  for (i32 i = 0; i < IMAGE_COUNT; i++) {
    s_push(atlas, (u64)i * 64u, 2, 2, (u64)(1000 + i));
  }
  REQUIRE(bk__atlas_flush(atlas));
  // Not one re-upload: every id was found in the grown index.
  REQUIRE(gpu.create_calls == IMAGE_COUNT);
  REQUIRE(gpu.get_pixels_calls == IMAGE_COUNT);

  bk__atlas_destroy(atlas);
  s_free_gpu(&gpu);
}

static void test_destroy_releases_every_texture(void) {
  FakeGpu gpu = {0};
  BK_AtlasDesc desc = s_make_desc(&gpu);
  BK_Atlas *atlas = bk__atlas_create(&desc);
  REQUIRE(atlas != nullptr);

  s_push(atlas, 1, 4, 4, 1);
  s_push(atlas, 2, 4, 4, 2);
  s_push(atlas, 3, 4, 4, 3);
  REQUIRE(bk__atlas_flush(atlas));
  REQUIRE(gpu.create_calls == 3);

  bk__atlas_destroy(atlas);
  REQUIRE(gpu.destroy_calls == 3);
  for (i32 i = 0; i < gpu.texture_count; i++) {
    REQUIRE(!gpu.textures[i].alive);
  }

  // Destroying null is a documented no-op, not a crash.
  bk__atlas_destroy(nullptr);

  s_free_gpu(&gpu);
}

// The sibling above only ever holds lonely textures. Atlassed records are skipped by
// bk__atlas_destroy's `!atlassed` guard on the record loop -- on purpose, since an atlassed
// record shares its handle with others and destroying it once per record would double-free
// -- but that guard only stays correct if the separate atlas_textures loop actually runs.
// Delete that loop and this is the only test that notices: fake_destroy_texture's
// REQUIRE(tex->alive) fires on a double destroy, never on a missing one, so a leaked atlas
// handle is silent everywhere else.
static void test_destroy_releases_atlas_textures(void) {
  FakeGpu gpu = {0};
  BK_AtlasDesc desc = s_make_desc(&gpu);
  desc.atlas_size = 256;
  desc.defrag_lonely_threshold = 2;
  BK_Atlas *atlas = bk__atlas_create(&desc);
  REQUIRE(atlas != nullptr);

  constexpr i32 IMAGE_COUNT = 3;
  for (i32 i = 0; i < IMAGE_COUNT; i++) {
    s_push(atlas, (u64)(i + 1), 8, 8, (u64)(i + 1));
  }
  REQUIRE(bk__atlas_flush(atlas));
  REQUIRE(bk__atlas_defrag(atlas));
  REQUIRE(gpu.create_calls == IMAGE_COUNT + 1); // the three lonely textures, then one atlas

  bk__atlas_destroy(atlas);
  REQUIRE(gpu.destroy_calls == IMAGE_COUNT + 1); // the three lonely ones, plus the atlas
  for (i32 i = 0; i < gpu.texture_count; i++) {
    REQUIRE(!gpu.textures[i].alive);
  }

  s_free_gpu(&gpu);
}

static void test_unavailable_image_is_dropped_and_the_frame_continues(void) {
  FakeGpu gpu = {0};
  gpu.fail_get_pixels = true;
  gpu.fail_get_pixels_image = 20;
  BK_AtlasDesc desc = s_make_desc(&gpu);
  BK_Atlas *atlas = bk__atlas_create(&desc);
  REQUIRE(atlas != nullptr);

  s_push(atlas, 10, 4, 4, 100);
  s_push(atlas, 20, 4, 4, 200); // get_pixels says no
  s_push(atlas, 30, 4, 4, 300);

  // False means "reported incompletely", not "abandoned" (spec section 4.1).
  REQUIRE(!bk__atlas_flush(atlas));

  // The entries pushed AFTER the failure still arrive. This is the assertion that
  // separates drop-and-continue from abandon-the-flush.
  REQUIRE(s_find_log(&gpu, 100) != nullptr);
  REQUIRE(s_find_log(&gpu, 300) != nullptr);
  REQUIRE(s_find_log(&gpu, 200) == nullptr);
  REQUIRE(gpu.log_count == 2);

  // No texture was created for the failed image.
  REQUIRE(gpu.create_calls == 2);

  bk__atlas_destroy(atlas);
  s_free_gpu(&gpu);
}

static void test_failed_texture_creation_behaves_like_a_missing_image(void) {
  FakeGpu gpu = {0};
  gpu.fail_create = true;
  gpu.fail_create_on_call = 2; // the second image's texture
  BK_AtlasDesc desc = s_make_desc(&gpu);
  BK_Atlas *atlas = bk__atlas_create(&desc);
  REQUIRE(atlas != nullptr);

  s_push(atlas, 10, 4, 4, 100);
  s_push(atlas, 20, 4, 4, 200); // create_texture returns 0
  s_push(atlas, 30, 4, 4, 300);

  REQUIRE(!bk__atlas_flush(atlas));
  REQUIRE(s_find_log(&gpu, 100) != nullptr);
  REQUIRE(s_find_log(&gpu, 300) != nullptr);
  REQUIRE(s_find_log(&gpu, 200) == nullptr);

  // fake_submit_batch already asserts no reported entry carries texture_id 0; this is the
  // path that would violate it if the drop were missing.
  REQUIRE(gpu.log_count == 2);

  bk__atlas_destroy(atlas);
  s_free_gpu(&gpu);
}

static void test_a_dropped_image_is_retried_not_remembered(void) {
  FakeGpu gpu = {0};
  gpu.fail_get_pixels = true;
  gpu.fail_get_pixels_image = 20;
  BK_AtlasDesc desc = s_make_desc(&gpu);
  BK_Atlas *atlas = bk__atlas_create(&desc);
  REQUIRE(atlas != nullptr);

  s_push(atlas, 20, 4, 4, 200);
  REQUIRE(!bk__atlas_flush(atlas));
  REQUIRE(gpu.log_count == 0);

  // The image becomes available. The failure was logged, not cached (spec section 4.1).
  gpu.fail_get_pixels = false;
  s_push(atlas, 20, 4, 4, 201);
  REQUIRE(bk__atlas_flush(atlas));
  REQUIRE(s_find_log(&gpu, 201) != nullptr);

  bk__atlas_destroy(atlas);
  s_free_gpu(&gpu);
}

// BK_AtlasGetPixelsFn's size parameter is i32, so an image whose byte count does not fit
// one cannot be described to the callback. The cache must drop it rather than pass a
// truncated or negative size -- which would make get_pixels write a different number of
// bytes than the buffer holds. Reachable because permanently-lonely images have no upper
// dimension bound (spec section 4.3).
//
// 65536 x 8192 x 4 == 2147483648, exactly INT32_MAX + 1. Nothing allocates 2 GB here: the
// guard runs before the pixel buffer is requested, which is the whole point.
static void test_an_image_too_large_to_describe_is_dropped(void) {
  FakeGpu gpu = {0};
  BK_AtlasDesc desc = s_make_desc(&gpu);
  desc.atlas_size = 256;
  BK_Atlas *atlas = bk__atlas_create(&desc);
  REQUIRE(atlas != nullptr);

  s_push(atlas, 1, 8, 8, 100);
  s_push(atlas, 2, 65536, 8192, 200);
  s_push(atlas, 3, 8, 8, 300);

  REQUIRE(!bk__atlas_flush(atlas));

  // Dropped without ever asking for its pixels or a texture, and the frame carries on --
  // entries pushed after it still report (spec section 4.1).
  REQUIRE(s_find_log(&gpu, 200) == nullptr);
  REQUIRE(s_find_log(&gpu, 100) != nullptr);
  REQUIRE(s_find_log(&gpu, 300) != nullptr);
  REQUIRE(gpu.get_pixels_calls == 2);
  REQUIRE(gpu.create_calls == 2);

  // No record survived for it, so a later push at a describable size is a first sighting.
  BK_AtlasEntry out = {0};
  REQUIRE(!bk__atlas_fetch(atlas, 2, &out));
  s_push(atlas, 2, 16, 16, 201);
  REQUIRE(bk__atlas_flush(atlas));
  REQUIRE(bk__atlas_fetch(atlas, 2, &out));
  s_require_rect_matches(&gpu, &out);

  bk__atlas_destroy(atlas);
  s_free_gpu(&gpu);
}

static void test_a_dropped_image_can_come_back_at_a_different_size(void) {
  FakeGpu gpu = {0};
  gpu.fail_get_pixels = true;
  gpu.fail_get_pixels_image = 20;
  BK_AtlasDesc desc = s_make_desc(&gpu);
  BK_Atlas *atlas = bk__atlas_create(&desc);
  REQUIRE(atlas != nullptr);

  s_push(atlas, 20, 4, 4, 200);
  REQUIRE(!bk__atlas_flush(atlas));
  REQUIRE(gpu.log_count == 0);

  // Available now, and at a different size than the failed attempt. The drop left no
  // record, so this is a first sighting and adopts 16x8. Had the drop kept the record,
  // s_make_resident's width/height assert would fire here -- on a caller that did
  // nothing wrong, because a failed image was never resident and owes no invalidate.
  gpu.fail_get_pixels = false;
  s_push(atlas, 20, 16, 8, 201);
  REQUIRE(bk__atlas_flush(atlas));

  const FakeLogEntry *logged = s_find_log(&gpu, 201);
  REQUIRE(logged != nullptr);
  REQUIRE(logged->entry.width == 16 && logged->entry.height == 8);
  REQUIRE(logged->entry.max_x == 16 && logged->entry.max_y == 8);
  s_require_rect_matches(&gpu, &logged->entry);

  bk__atlas_destroy(atlas);
  s_free_gpu(&gpu);
}

static void test_a_failing_flush_still_drains_the_push_buffer(void) {
  FakeGpu gpu = {0};
  gpu.fail_get_pixels = true;
  gpu.fail_get_pixels_image = 20;
  BK_AtlasDesc desc = s_make_desc(&gpu);
  BK_Atlas *atlas = bk__atlas_create(&desc);
  REQUIRE(atlas != nullptr);

  s_push(atlas, 10, 4, 4, 100);
  s_push(atlas, 20, 4, 4, 200);
  REQUIRE(!bk__atlas_flush(atlas));
  REQUIRE(gpu.log_count == 1);

  // Nothing may replay. A failure partway through must not leave entries buffered against
  // a frame that never happened (spec section 4.1).
  gpu.fail_get_pixels = false;
  REQUIRE(bk__atlas_flush(atlas));
  REQUIRE(gpu.log_count == 1);
  REQUIRE(gpu.batch_count == 1);

  bk__atlas_destroy(atlas);
  s_free_gpu(&gpu);
}

static void test_a_missing_images_failure_is_logged_once_per_lifetime(void) {
  FakeGpu gpu = {0};
  gpu.fail_get_pixels = true;
  gpu.fail_get_pixels_image = 20;
  BK_AtlasDesc desc = s_make_desc(&gpu);
  BK_Atlas *atlas = bk__atlas_create(&desc);
  REQUIRE(atlas != nullptr);

  SDL_LogOutputFunction prev_fn;
  void *prev_userdata;
  SDL_GetLogOutputFunction(&prev_fn, &prev_userdata);
  LogCapture capture = {.watch_image_id = 20};
  SDL_SetLogOutputFunction(s_capture_log, &capture);

  s_push(atlas, 20, 4, 4, 200);
  REQUIRE(!bk__atlas_flush(atlas));
  // Still failing. A frame loop retries every frame; the log line must not repeat (spec
  // section 4.1) or a permanently missing asset would spam sixty lines a second.
  s_push(atlas, 20, 4, 4, 201);
  REQUIRE(!bk__atlas_flush(atlas));

  SDL_SetLogOutputFunction(prev_fn, prev_userdata);
  REQUIRE(capture.count == 1);

  bk__atlas_destroy(atlas);
  s_free_gpu(&gpu);
}

static void test_prefetch_makes_an_image_resident_without_a_push(void) {
  FakeGpu gpu = {0};
  BK_AtlasDesc desc = s_make_desc(&gpu);
  BK_Atlas *atlas = bk__atlas_create(&desc);
  REQUIRE(atlas != nullptr);

  // Immediate: get_pixels and create_texture run before prefetch returns (spec section 3).
  REQUIRE(bk__atlas_prefetch(atlas, 9, 4, 4));
  REQUIRE(gpu.get_pixels_calls == 1);
  REQUIRE(gpu.create_calls == 1);
  REQUIRE(gpu.batch_count == 0); // prefetch draws nothing

  // The later push finds it already there and pays nothing.
  s_push(atlas, 9, 4, 4, 900);
  REQUIRE(bk__atlas_flush(atlas));
  REQUIRE(gpu.get_pixels_calls == 1);
  REQUIRE(gpu.create_calls == 1);
  REQUIRE(s_find_log(&gpu, 900) != nullptr);

  // Prefetching something already resident is a no-op that reports success.
  REQUIRE(bk__atlas_prefetch(atlas, 9, 4, 4));
  REQUIRE(gpu.create_calls == 1);

  bk__atlas_destroy(atlas);
  s_free_gpu(&gpu);
}

static void test_prefetch_reports_an_unavailable_image(void) {
  FakeGpu gpu = {0};
  gpu.fail_get_pixels = true;
  gpu.fail_get_pixels_image = 9;
  BK_AtlasDesc desc = s_make_desc(&gpu);
  BK_Atlas *atlas = bk__atlas_create(&desc);
  REQUIRE(atlas != nullptr);

  REQUIRE(!bk__atlas_prefetch(atlas, 9, 4, 4));
  REQUIRE(gpu.create_calls == 0);

  bk__atlas_destroy(atlas);
  s_free_gpu(&gpu);
}

static void test_fetch_reports_residency_without_creating_it(void) {
  FakeGpu gpu = {0};
  BK_AtlasDesc desc = s_make_desc(&gpu);
  BK_Atlas *atlas = bk__atlas_create(&desc);
  REQUIRE(atlas != nullptr);

  // A sentinel the cache must not touch. CF's fetch signals "not found" with a zeroed
  // struct a caller can mistake for a valid entry at rect (0,0,0,0) -- this one leaves
  // *out alone (spec section 3.1).
  BK_AtlasEntry out = {.image_id = 0xDEAD, .texture_id = 0xBEEF, .max_x = 1234};
  REQUIRE(!bk__atlas_fetch(atlas, 9, &out));
  REQUIRE(out.image_id == 0xDEAD);
  REQUIRE(out.texture_id == 0xBEEF);
  REQUIRE(out.max_x == 1234);

  // fetch never makes anything resident -- that is prefetch's job, and only prefetch's.
  REQUIRE(gpu.get_pixels_calls == 0);
  REQUIRE(gpu.create_calls == 0);

  REQUIRE(bk__atlas_prefetch(atlas, 9, 8, 4));
  REQUIRE(bk__atlas_fetch(atlas, 9, &out));
  REQUIRE(out.image_id == 9);
  REQUIRE(out.texture_id != 0);
  REQUIRE(out.width == 8 && out.height == 4);
  REQUIRE(out.min_x == 0 && out.min_y == 0);
  REQUIRE(out.max_x == 8 && out.max_y == 4);
  s_require_rect_matches(&gpu, &out);

  bk__atlas_destroy(atlas);
  s_free_gpu(&gpu);
}

static void test_invalidate_forgets_a_lonely_image(void) {
  FakeGpu gpu = {0};
  BK_AtlasDesc desc = s_make_desc(&gpu);
  BK_Atlas *atlas = bk__atlas_create(&desc);
  REQUIRE(atlas != nullptr);

  s_push(atlas, 9, 4, 4, 900);
  REQUIRE(bk__atlas_flush(atlas));
  REQUIRE(gpu.create_calls == 1);
  u64 first_texture = gpu.textures[0].id;

  bk__atlas_invalidate(atlas, 9);
  REQUIRE(gpu.destroy_calls == 1);
  REQUIRE(!s_find_texture(&gpu, first_texture)->alive);

  BK_AtlasEntry out = {0};
  REQUIRE(!bk__atlas_fetch(atlas, 9, &out)); // the record is gone, not just the pixels

  // The next push re-fetches, and may do so at a new size -- which is the documented way
  // to resize an image (spec section 4.3).
  s_push(atlas, 9, 16, 8, 901);
  REQUIRE(bk__atlas_flush(atlas));
  REQUIRE(gpu.get_pixels_calls == 2);
  REQUIRE(gpu.create_calls == 2);
  REQUIRE(bk__atlas_fetch(atlas, 9, &out));
  REQUIRE(out.width == 16 && out.height == 8);
  s_require_rect_matches(&gpu, &out);

  // Invalidating something unknown is a no-op, not a crash.
  bk__atlas_invalidate(atlas, 12345);

  bk__atlas_destroy(atlas);
  s_free_gpu(&gpu);
}

static void test_tick_ages_only_images_that_stop_being_pushed(void) {
  FakeGpu gpu = {0};
  BK_AtlasDesc desc = s_make_desc(&gpu);
  desc.ticks_until_decay = 4;
  BK_Atlas *atlas = bk__atlas_create(&desc);
  REQUIRE(atlas != nullptr);

  s_push(atlas, 10, 4, 4, 100);
  s_push(atlas, 20, 4, 4, 200);
  REQUIRE(bk__atlas_flush(atlas));

  // Ten ticks pass. Image 10 keeps being drawn; image 20 does not.
  for (i32 i = 0; i < 10; i++) {
    bk__atlas_tick(atlas);
    s_push(atlas, 10, 4, 4, 100);
    REQUIRE(bk__atlas_flush(atlas));
  }

  // Decay is bookkeeping until defrag acts on it (spec section 4.4): both are still
  // resident and neither texture has been destroyed.
  REQUIRE(gpu.destroy_calls == 0);
  BK_AtlasEntry out = {0};
  REQUIRE(bk__atlas_fetch(atlas, 20, &out));

  // What this test pins down is that ticking is pure bookkeeping: it advances the clock
  // and evicts nothing on its own, however far an entry has fallen behind. Whether the
  // stamps it leaves behind are actually correct -- and Task 5 asserts that defrag then
  // evicts 20 and keeps 10 -- is only observable once defrag exists to consume them.
  REQUIRE(bk__atlas_fetch(atlas, 10, &out));

  bk__atlas_destroy(atlas);
  s_free_gpu(&gpu);
}

static void test_flush_alone_never_builds_an_atlas(void) {
  FakeGpu gpu = {0};
  BK_AtlasDesc desc = s_make_desc(&gpu);
  desc.atlas_size = 256;
  desc.defrag_lonely_threshold = 4;
  BK_Atlas *atlas = bk__atlas_create(&desc);
  REQUIRE(atlas != nullptr);

  // Far more than the threshold, flushed repeatedly. Without a defrag call, every image
  // stays on its own texture forever (spec section 4.3).
  for (i32 frame = 0; frame < 3; frame++) {
    for (i32 i = 0; i < 20; i++) {
      s_push(atlas, (u64)(i + 1), 8, 8, (u64)(i + 1));
    }
    REQUIRE(bk__atlas_flush(atlas));
    bk__atlas_tick(atlas);
  }
  REQUIRE(gpu.create_calls == 20); // 20 lonely textures, no atlas
  REQUIRE(gpu.batch_count == 60);  // 20 batches per frame: the thing defrag exists to fix

  bk__atlas_destroy(atlas);
  s_free_gpu(&gpu);
}

static void test_defrag_packs_lonely_images_into_one_atlas(void) {
  FakeGpu gpu = {0};
  BK_AtlasDesc desc = s_make_desc(&gpu);
  desc.atlas_size = 256;
  desc.defrag_lonely_threshold = 4;
  BK_Atlas *atlas = bk__atlas_create(&desc);
  REQUIRE(atlas != nullptr);

  constexpr i32 THRESHOLD = 4;
  for (i32 i = 0; i < THRESHOLD; i++) {
    s_push(atlas, (u64)(i + 1), 8, 8, (u64)(i + 1));
  }
  REQUIRE(bk__atlas_flush(atlas));
  REQUIRE(gpu.create_calls == THRESHOLD);

  // Exactly at the threshold: spec section 4.3 says "more than," not "at least," so this
  // must not pack. A gate off by one (`count < threshold` instead of `<=`) would pack right
  // here -- this is the boundary a fixture of threshold+1 images can never reach, since
  // count and threshold are never equal there.
  REQUIRE(bk__atlas_defrag(atlas));
  REQUIRE(gpu.create_calls == THRESHOLD); // still lonely: no atlas built
  for (i32 i = 0; i < THRESHOLD; i++) {
    BK_AtlasEntry entry = {0};
    REQUIRE(bk__atlas_fetch(atlas, (u64)(i + 1), &entry));
    FakeTexture *tex = s_find_texture(&gpu, entry.texture_id);
    REQUIRE(tex->width == 8); // its own texture, not a 256x256 atlas
  }

  constexpr i32 IMAGE_COUNT = THRESHOLD + 1; // one more than the threshold
  s_push(atlas, (u64)IMAGE_COUNT, 8, 8, (u64)IMAGE_COUNT);
  REQUIRE(bk__atlas_flush(atlas));
  REQUIRE(gpu.create_calls == IMAGE_COUNT);
  REQUIRE(gpu.batch_count == IMAGE_COUNT);

  REQUIRE(bk__atlas_defrag(atlas));

  // One atlas texture created, and the five individual ones destroyed.
  REQUIRE(gpu.create_calls == IMAGE_COUNT + 1);
  REQUIRE(gpu.destroy_calls == IMAGE_COUNT);

  // Now they share a texture: one batch, all five entries, in push order.
  for (i32 i = 0; i < IMAGE_COUNT; i++) {
    s_push(atlas, (u64)(i + 1), 8, 8, (u64)(100 + i));
  }
  REQUIRE(bk__atlas_flush(atlas));
  REQUIRE(gpu.batch_count == IMAGE_COUNT + 1); // the five singles, then one shared batch
  for (i32 i = 0; i < IMAGE_COUNT; i++) {
    const FakeLogEntry *logged = s_find_log(&gpu, (u64)(100 + i));
    REQUIRE(logged != nullptr);
    REQUIRE(logged->batch_index == IMAGE_COUNT); // all in the same, last batch
  }

  // Packing did not re-fetch: the pack reads pixels once, during defrag.
  REQUIRE(gpu.get_pixels_calls == IMAGE_COUNT * 2);

  bk__atlas_destroy(atlas);
  s_free_gpu(&gpu);
}

static void test_an_image_looks_the_same_lonely_and_atlassed(void) {
  FakeGpu gpu = {0};
  BK_AtlasDesc desc = s_make_desc(&gpu);
  desc.atlas_size = 256;
  desc.defrag_lonely_threshold = 2;
  BK_Atlas *atlas = bk__atlas_create(&desc);
  REQUIRE(atlas != nullptr);

  // Deliberately non-square and mismatched, so a transposed or y-flipped blit cannot
  // coincide with a correct one. This is the guard from spec section 3.3.
  //
  // Heights 5/9/14 sort to 14/9/5 by the pack's tallest-first order, and all three still fit
  // on one shelf, so every placement here lands at min_y == 0 -- a bug that always reported
  // min_y as 0 would pass this test. test_images_that_do_not_fit_stay_pending is what
  // actually exercises min_y != 0; do not remove it under the assumption this test covers it.
  s_push(atlas, 11, 12, 5, 1);
  s_push(atlas, 22, 7, 9, 2);
  s_push(atlas, 33, 3, 14, 3);
  REQUIRE(bk__atlas_flush(atlas));

  BK_AtlasEntry lonely_11 = {0};
  REQUIRE(bk__atlas_fetch(atlas, 11, &lonely_11));
  REQUIRE(lonely_11.min_x == 0 && lonely_11.min_y == 0);
  s_require_rect_matches(&gpu, &lonely_11);

  REQUIRE(bk__atlas_defrag(atlas));

  BK_AtlasEntry atlassed_11 = {0};
  REQUIRE(bk__atlas_fetch(atlas, 11, &atlassed_11));
  REQUIRE(atlassed_11.texture_id != lonely_11.texture_id); // it really moved
  REQUIRE(atlassed_11.width == 12 && atlassed_11.height == 5);
  // Same pixels, same orientation, at the new rect. A y-flip on one path only -- which is
  // what the donor does -- fails right here.
  s_require_rect_matches(&gpu, &atlassed_11);

  for (u64 image_id = 22; image_id <= 33; image_id += 11) {
    BK_AtlasEntry entry = {0};
    REQUIRE(bk__atlas_fetch(atlas, image_id, &entry));
    s_require_rect_matches(&gpu, &entry);
  }

  bk__atlas_destroy(atlas);
  s_free_gpu(&gpu);
}

static void test_oversized_images_stay_lonely_and_do_not_gate_the_threshold(void) {
  FakeGpu gpu = {0};
  BK_AtlasDesc desc = s_make_desc(&gpu);
  desc.atlas_size = 256; // half is 128
  desc.defrag_lonely_threshold = 3;
  BK_Atlas *atlas = bk__atlas_create(&desc);
  REQUIRE(atlas != nullptr);

  // Three oversized images -- exactly the threshold. If they counted, the four small ones
  // below would push the total to seven and pack anyway, hiding the bug. They must not
  // count, so it is the small images alone that have to cross it (spec section 4.3).
  s_push(atlas, 1, 200, 8, 1);   // width > 128
  s_push(atlas, 2, 8, 200, 2);   // height > 128
  s_push(atlas, 3, 300, 300, 3); // larger than the atlas outright: still legal
  REQUIRE(bk__atlas_flush(atlas));
  REQUIRE(bk__atlas_defrag(atlas));
  REQUIRE(gpu.create_calls == 3); // no atlas: three oversized images are not candidates

  for (i32 i = 0; i < 4; i++) {
    s_push(atlas, (u64)(10 + i), 8, 8, (u64)(10 + i));
  }
  REQUIRE(bk__atlas_flush(atlas));
  REQUIRE(gpu.create_calls == 7);
  REQUIRE(bk__atlas_defrag(atlas));
  REQUIRE(gpu.create_calls == 8);  // four small images crossed the threshold on their own
  REQUIRE(gpu.destroy_calls == 4); // and only their textures were destroyed

  // The oversized ones are untouched and still report their whole texture.
  BK_AtlasEntry entry = {0};
  REQUIRE(bk__atlas_fetch(atlas, 3, &entry));
  REQUIRE(entry.min_x == 0 && entry.min_y == 0);
  REQUIRE(entry.max_x == 300 && entry.max_y == 300);

  bk__atlas_destroy(atlas);
  s_free_gpu(&gpu);
}

static void test_images_that_do_not_fit_stay_pending(void) {
  FakeGpu gpu = {0};
  BK_AtlasDesc desc = s_make_desc(&gpu);
  desc.atlas_size = 64;
  desc.defrag_lonely_threshold = 2;
  BK_Atlas *atlas = bk__atlas_create(&desc);
  REQUIRE(atlas != nullptr);

  // Each image is 32x32 and the atlas is 64x64, so exactly four fit. Push six.
  constexpr i32 IMAGE_COUNT = 6;
  for (i32 i = 0; i < IMAGE_COUNT; i++) {
    s_push(atlas, (u64)(i + 1), 32, 32, (u64)(i + 1));
  }
  REQUIRE(bk__atlas_flush(atlas));
  REQUIRE(gpu.create_calls == IMAGE_COUNT);

  REQUIRE(bk__atlas_defrag(atlas));

  // Four packed into one atlas; two left over. Their pixels still live on their own
  // textures, so they are still drawable.
  i32 atlassed = 0;
  i32 still_lonely = 0;
  for (i32 i = 0; i < IMAGE_COUNT; i++) {
    BK_AtlasEntry entry = {0};
    REQUIRE(bk__atlas_fetch(atlas, (u64)(i + 1), &entry));
    s_require_rect_matches(&gpu, &entry);
    FakeTexture *tex = s_find_texture(&gpu, entry.texture_id);
    if (tex->width == 64) {
      atlassed++;
    } else {
      still_lonely++;
    }
  }
  REQUIRE(atlassed == 4);
  REQUIRE(still_lonely == 2);

  bk__atlas_destroy(atlas);
  s_free_gpu(&gpu);
}

static void test_a_failed_atlas_leaves_every_image_drawable(void) {
  FakeGpu gpu = {0};
  BK_AtlasDesc desc = s_make_desc(&gpu);
  desc.atlas_size = 256;
  desc.defrag_lonely_threshold = 2;
  BK_Atlas *atlas = bk__atlas_create(&desc);
  REQUIRE(atlas != nullptr);

  constexpr i32 IMAGE_COUNT = 4;
  for (i32 i = 0; i < IMAGE_COUNT; i++) {
    s_push(atlas, (u64)(i + 1), 8, 8, (u64)(i + 1));
  }
  REQUIRE(bk__atlas_flush(atlas));
  REQUIRE(gpu.create_calls == IMAGE_COUNT);

  // The next create_texture call is the atlas's, and it fails.
  gpu.fail_create = true;
  gpu.fail_create_on_call = IMAGE_COUNT + 1;
  REQUIRE(!bk__atlas_defrag(atlas));

  // Nothing was destroyed. Destroying the individual textures before the atlas handle
  // exists would leave four images with no pixels anywhere -- which is why the destroy
  // loop runs only after create_texture returns non-zero.
  REQUIRE(gpu.destroy_calls == 0);
  for (i32 i = 0; i < IMAGE_COUNT; i++) {
    BK_AtlasEntry entry = {0};
    REQUIRE(bk__atlas_fetch(atlas, (u64)(i + 1), &entry));
    REQUIRE(s_find_texture(&gpu, entry.texture_id)->alive);
    s_require_rect_matches(&gpu, &entry);
  }

  // And the pack still works once the fault clears.
  gpu.fail_create = false;
  REQUIRE(bk__atlas_defrag(atlas));
  REQUIRE(gpu.destroy_calls == IMAGE_COUNT);

  bk__atlas_destroy(atlas);
  s_free_gpu(&gpu);
}

// None of the brief's own tests inject a get_pixels failure mid-pack, which leaves the one
// hazard the whole image_id-keyed design exists to prevent completely unexercised:
// s_record_remove swap-removes on a drop, relocating whatever record was last in the array.
// Here that is image 3's record, moved into the slot image 2 just vacated. Step 7 must
// re-look-up every survivor by image_id rather than trust an index computed before the
// drop -- this is the test that would catch a cached-index regression.
static void test_a_get_pixels_failure_during_pack_does_not_corrupt_neighbors(void) {
  FakeGpu gpu = {0};
  BK_AtlasDesc desc = s_make_desc(&gpu);
  desc.atlas_size = 256;
  desc.defrag_lonely_threshold = 2;
  BK_Atlas *atlas = bk__atlas_create(&desc);
  REQUIRE(atlas != nullptr);

  s_push(atlas, 1, 8, 8, 1);
  s_push(atlas, 2, 8, 8, 2);
  s_push(atlas, 3, 8, 8, 3);
  REQUIRE(bk__atlas_flush(atlas));
  REQUIRE(gpu.create_calls == 3);

  gpu.fail_get_pixels = true;
  gpu.fail_get_pixels_image = 2;
  // Dropping one image mid-pack is not a hard failure -- the pack still runs to completion
  // -- but it is still "did less than asked", the same meaning bk__atlas_flush's return
  // value already carries, so defrag reports it the same way.
  REQUIRE(!bk__atlas_defrag(atlas));

  REQUIRE(gpu.create_calls == 4);  // one atlas, built from the two survivors
  REQUIRE(gpu.destroy_calls == 3); // image 2's lonely texture, plus 1's and 3's

  BK_AtlasEntry dropped = {0};
  REQUIRE(!bk__atlas_fetch(atlas, 2, &dropped)); // dropped outright, not just its texture

  BK_AtlasEntry entry_1 = {0};
  BK_AtlasEntry entry_3 = {0};
  REQUIRE(bk__atlas_fetch(atlas, 1, &entry_1));
  REQUIRE(bk__atlas_fetch(atlas, 3, &entry_3));
  // If image 3 had been adopted through a stale index left over from before image 2's
  // removal, this would either read the wrong record or the wrong pixels.
  s_require_rect_matches(&gpu, &entry_1);
  s_require_rect_matches(&gpu, &entry_3);

  bk__atlas_destroy(atlas);
  s_free_gpu(&gpu);
}

// The brief's own callout: if every placement in a pack fails get_pixels, s_pack_one_atlas
// must free its buffers and return 0 without ever calling create_texture -- otherwise it
// mints a blank atlas no record points at, which Task 5's dissolve pass then skips forever
// via its `total > 0` guard, and only bk__atlas_destroy ever reclaims it. FakeGpu's
// single-image fault injection can't reach this path on its own: every candidate here is
// well under atlas_size / 2, so at least two always survive a pack. fail_get_pixels_all
// extends the fake the same way its other fault-injection fields already do, rather than
// redesigning it.
static void test_a_pack_where_every_image_fails_builds_nothing(void) {
  FakeGpu gpu = {0};
  BK_AtlasDesc desc = s_make_desc(&gpu);
  desc.atlas_size = 256;
  desc.defrag_lonely_threshold = 2;
  BK_Atlas *atlas = bk__atlas_create(&desc);
  REQUIRE(atlas != nullptr);

  constexpr i32 IMAGE_COUNT = 4; // more than the threshold
  for (i32 i = 0; i < IMAGE_COUNT; i++) {
    s_push(atlas, (u64)(i + 1), 8, 8, (u64)(i + 1));
  }
  REQUIRE(bk__atlas_flush(atlas));
  REQUIRE(gpu.create_calls == IMAGE_COUNT);

  gpu.fail_get_pixels_all = true;
  REQUIRE(!bk__atlas_defrag(atlas));         // every image dropped is still "did less than asked"
  REQUIRE(gpu.create_calls == IMAGE_COUNT);  // no atlas: nothing survived to build one from
  REQUIRE(gpu.destroy_calls == IMAGE_COUNT); // every lonely texture destroyed on its drop

  // Every image was dropped outright, not just its texture.
  for (i32 i = 0; i < IMAGE_COUNT; i++) {
    BK_AtlasEntry entry = {0};
    REQUIRE(!bk__atlas_fetch(atlas, (u64)(i + 1), &entry));
  }

  bk__atlas_destroy(atlas);
  s_free_gpu(&gpu);
}

static void test_defrag_evicts_what_stopped_being_drawn(void) {
  FakeGpu gpu = {0};
  BK_AtlasDesc desc = s_make_desc(&gpu);
  desc.ticks_until_decay = 3;
  desc.defrag_lonely_threshold = 100; // never pack: this test is about decay alone
  BK_Atlas *atlas = bk__atlas_create(&desc);
  REQUIRE(atlas != nullptr);

  s_push(atlas, 10, 4, 4, 100);
  s_push(atlas, 20, 4, 4, 200);
  REQUIRE(bk__atlas_flush(atlas));
  REQUIRE(gpu.create_calls == 2);

  // Image 10 keeps being drawn; image 20 stops.
  for (i32 i = 0; i < 5; i++) {
    bk__atlas_tick(atlas);
    s_push(atlas, 10, 4, 4, 100);
    REQUIRE(bk__atlas_flush(atlas));
  }

  REQUIRE(bk__atlas_defrag(atlas));

  BK_AtlasEntry entry = {0};
  REQUIRE(bk__atlas_fetch(atlas, 10, &entry));  // still drawn, so still resident
  REQUIRE(!bk__atlas_fetch(atlas, 20, &entry)); // decayed and evicted
  REQUIRE(gpu.destroy_calls == 1);

  bk__atlas_destroy(atlas);
  s_free_gpu(&gpu);
}

static void test_a_mostly_decayed_atlas_is_dissolved_and_repacked(void) {
  FakeGpu gpu = {0};
  BK_AtlasDesc desc = s_make_desc(&gpu);
  desc.atlas_size = 256;
  desc.ticks_until_decay = 3;
  desc.defrag_lonely_threshold = 3;
  BK_Atlas *atlas = bk__atlas_create(&desc);
  REQUIRE(atlas != nullptr);

  // Six images into one atlas.
  constexpr i32 IMAGE_COUNT = 6;
  for (i32 i = 0; i < IMAGE_COUNT; i++) {
    s_push(atlas, (u64)(i + 1), 8, 8, (u64)(i + 1));
  }
  REQUIRE(bk__atlas_flush(atlas));
  REQUIRE(bk__atlas_defrag(atlas));
  REQUIRE(gpu.create_calls == IMAGE_COUNT + 1);

  BK_AtlasEntry before = {0};
  REQUIRE(bk__atlas_fetch(atlas, 1, &before));
  u64 first_atlas = before.texture_id;

  // Exactly three of the six stop being drawn. Sitting on the boundary is the point: the
  // rule is decayed/total >= 1/2, so 3/6 must dissolve. A test at 4/6 would pass under a
  // strict > just as happily, and the whole reason this threshold is written out in the
  // spec is that the donor's version of it is wrong (spec section 4.4).
  for (i32 i = 0; i < 5; i++) {
    bk__atlas_tick(atlas);
    s_push(atlas, 1, 8, 8, 1);
    s_push(atlas, 2, 8, 8, 2);
    s_push(atlas, 3, 8, 8, 3);
    REQUIRE(bk__atlas_flush(atlas));
  }

  // This call dissolves the atlas but, despite the test's name, does not actually repack:
  // the three survivors sit exactly at defrag_lonely_threshold (3 <= 3), so the pack pass
  // returns 0 without placing them. test_dissolved_survivors_repack_within_the_same_defrag
  // is the one that exercises an in-call repack; this test carries the dissolve half only.
  REQUIRE(bk__atlas_defrag(atlas));

  REQUIRE(!s_find_texture(&gpu, first_atlas)->alive); // the old atlas is gone
  BK_AtlasEntry entry = {0};
  for (u64 image_id = 4; image_id <= IMAGE_COUNT; image_id++) {
    REQUIRE(!bk__atlas_fetch(atlas, image_id, &entry)); // the decayed three are dropped
  }

  // Survivors are still drawable: the next push re-creates a texture for them lazily,
  // rather than the dissolve re-uploading every survivor on the spot (spec section 3.1).
  s_push(atlas, 1, 8, 8, 11);
  s_push(atlas, 2, 8, 8, 22);
  s_push(atlas, 3, 8, 8, 33);
  REQUIRE(bk__atlas_flush(atlas));
  REQUIRE(s_find_log(&gpu, 11) != nullptr);
  REQUIRE(s_find_log(&gpu, 22) != nullptr);
  REQUIRE(s_find_log(&gpu, 33) != nullptr);
  REQUIRE(bk__atlas_fetch(atlas, 1, &entry));
  s_require_rect_matches(&gpu, &entry);

  bk__atlas_destroy(atlas);
  s_free_gpu(&gpu);
}

static void test_a_lightly_decayed_atlas_is_left_alone(void) {
  FakeGpu gpu = {0};
  BK_AtlasDesc desc = s_make_desc(&gpu);
  desc.atlas_size = 256;
  desc.ticks_until_decay = 3;
  desc.defrag_lonely_threshold = 3;
  BK_Atlas *atlas = bk__atlas_create(&desc);
  REQUIRE(atlas != nullptr);

  constexpr i32 IMAGE_COUNT = 6;
  for (i32 i = 0; i < IMAGE_COUNT; i++) {
    s_push(atlas, (u64)(i + 1), 8, 8, (u64)(i + 1));
  }
  REQUIRE(bk__atlas_flush(atlas));
  REQUIRE(bk__atlas_defrag(atlas));

  BK_AtlasEntry before = {0};
  REQUIRE(bk__atlas_fetch(atlas, 1, &before));
  u64 first_atlas = before.texture_id;

  // Only two of six stop being drawn: 2/6 is below half, so the atlas stays (spec 4.4).
  for (i32 i = 0; i < 5; i++) {
    bk__atlas_tick(atlas);
    for (i32 image = 1; image <= 4; image++) {
      s_push(atlas, (u64)image, 8, 8, (u64)image);
    }
    REQUIRE(bk__atlas_flush(atlas));
  }

  REQUIRE(bk__atlas_defrag(atlas));
  REQUIRE(s_find_texture(&gpu, first_atlas)->alive);

  BK_AtlasEntry entry = {0};
  REQUIRE(bk__atlas_fetch(atlas, 1, &entry));
  REQUIRE(entry.texture_id == first_atlas);
  REQUIRE(entry.min_x == before.min_x && entry.min_y == before.min_y); // not repacked
  // Even the decayed ones are still there: nothing evicts them until the atlas goes.
  REQUIRE(bk__atlas_fetch(atlas, 5, &entry));

  bk__atlas_destroy(atlas);
  s_free_gpu(&gpu);
}

// Every other decay/dissolve test above holds at most one live atlas, which makes
// bk__atlas_defrag's dissolve walk over atlas_textures a self-assignment every time it
// swap-removes: there is only ever one slot. That leaves its backwards direction
// undiscriminated -- a forward walk would pass every test above, because dissolving the
// first of two atlases would swap the second into its slot and then skip past it.
//
// atlas_size 64 with eight 32x32 images forces two atlases from one defrag call (four fit
// per atlas, and a threshold of one keeps the pack loop going for a second). Decaying both
// fully, rather than just one, is what makes forward and backward walks disagree: if only
// one atlas decayed, either direction would reach it eventually.
static void test_defrag_dissolves_every_fully_decayed_atlas(void) {
  FakeGpu gpu = {0};
  BK_AtlasDesc desc = s_make_desc(&gpu);
  desc.atlas_size = 64;
  desc.ticks_until_decay = 3;
  desc.defrag_lonely_threshold = 1;
  BK_Atlas *atlas = bk__atlas_create(&desc);
  REQUIRE(atlas != nullptr);

  constexpr i32 IMAGE_COUNT = 8;
  for (i32 i = 0; i < IMAGE_COUNT; i++) {
    s_push(atlas, (u64)(i + 1), 32, 32, (u64)(i + 1));
  }
  REQUIRE(bk__atlas_flush(atlas));
  REQUIRE(gpu.create_calls == IMAGE_COUNT);

  REQUIRE(bk__atlas_defrag(atlas));
  REQUIRE(gpu.create_calls == IMAGE_COUNT + 2); // two atlases, four images each

  BK_AtlasEntry entry = {0};
  REQUIRE(bk__atlas_fetch(atlas, 1, &entry));
  u64 atlas_a = entry.texture_id;
  u64 atlas_b = 0;
  for (u64 image_id = 2; image_id <= IMAGE_COUNT; image_id++) {
    REQUIRE(bk__atlas_fetch(atlas, image_id, &entry));
    if (entry.texture_id != atlas_a) {
      atlas_b = entry.texture_id;
      break;
    }
  }
  REQUIRE(atlas_b != 0);
  REQUIRE(atlas_b != atlas_a);

  // Stop drawing all eight, and tick past decay so both atlases are fully decayed.
  for (i32 i = 0; i < 5; i++) {
    bk__atlas_tick(atlas);
  }

  REQUIRE(bk__atlas_defrag(atlas));

  REQUIRE(!s_find_texture(&gpu, atlas_a)->alive);
  REQUIRE(!s_find_texture(&gpu, atlas_b)->alive);

  bk__atlas_destroy(atlas);
  s_free_gpu(&gpu);
}

static void test_invalidating_an_atlassed_image_dissolves_its_atlas(void) {
  FakeGpu gpu = {0};
  BK_AtlasDesc desc = s_make_desc(&gpu);
  desc.atlas_size = 256;
  desc.defrag_lonely_threshold = 2;
  BK_Atlas *atlas = bk__atlas_create(&desc);
  REQUIRE(atlas != nullptr);

  for (i32 i = 0; i < 3; i++) {
    s_push(atlas, (u64)(i + 1), 8, 8, (u64)(i + 1));
  }
  REQUIRE(bk__atlas_flush(atlas));
  REQUIRE(bk__atlas_defrag(atlas));

  BK_AtlasEntry entry = {0};
  REQUIRE(bk__atlas_fetch(atlas, 1, &entry));
  u64 atlas_texture = entry.texture_id;
  i32 fetches_before = gpu.get_pixels_calls;

  bk__atlas_invalidate(atlas, 1);

  REQUIRE(!s_find_texture(&gpu, atlas_texture)->alive);
  REQUIRE(!bk__atlas_fetch(atlas, 1, &entry)); // the invalidated image's record is gone
  // Its neighbours keep their records but lose their texture, so fetch reports them as
  // not resident until their next push re-uploads them (spec section 3, invalidate).
  REQUIRE(!bk__atlas_fetch(atlas, 2, &entry));

  // Everyone re-uploads on the next push, including the invalidated image at a new size.
  s_push(atlas, 1, 16, 16, 100);
  s_push(atlas, 2, 8, 8, 200);
  s_push(atlas, 3, 8, 8, 300);
  REQUIRE(bk__atlas_flush(atlas));
  REQUIRE(gpu.get_pixels_calls == fetches_before + 3);
  REQUIRE(bk__atlas_fetch(atlas, 1, &entry));
  REQUIRE(entry.width == 16 && entry.height == 16);
  s_require_rect_matches(&gpu, &entry);

  bk__atlas_destroy(atlas);
  s_free_gpu(&gpu);
}

// Row 4 of the mutation table (leaving `atlassed` true on a dissolved atlas's survivors) is
// invisible to test_a_mostly_decayed_atlas_is_dissolved_and_repacked: that test always
// re-pushes its survivors before checking anything, and s_make_resident resets `atlassed`
// on any re-upload regardless of what the dissolve left behind. The bug only shows up
// between the dissolve and a push -- specifically in whether s_pack_one_atlas's candidate
// filter (`!atlassed`) can see the survivors at all -- which only defrag's own pack pass
// can exercise, in the very call that dissolved them (spec section 4.4 step 1: "step 3 may
// re-pack it in this same call").
static void test_dissolved_survivors_repack_within_the_same_defrag(void) {
  FakeGpu gpu = {0};
  BK_AtlasDesc desc = s_make_desc(&gpu);
  desc.atlas_size = 256;
  desc.ticks_until_decay = 3;
  desc.defrag_lonely_threshold = 1; // low enough that the 3 survivors alone cross it
  BK_Atlas *atlas = bk__atlas_create(&desc);
  REQUIRE(atlas != nullptr);

  constexpr i32 IMAGE_COUNT = 6;
  for (i32 i = 0; i < IMAGE_COUNT; i++) {
    s_push(atlas, (u64)(i + 1), 8, 8, (u64)(i + 1));
  }
  REQUIRE(bk__atlas_flush(atlas));
  REQUIRE(bk__atlas_defrag(atlas));
  i32 create_calls_before_dissolve = gpu.create_calls;

  // Three of six stop being drawn -- the same 3-of-6 dissolve boundary as the sibling test.
  for (i32 i = 0; i < 5; i++) {
    bk__atlas_tick(atlas);
    s_push(atlas, 1, 8, 8, 1);
    s_push(atlas, 2, 8, 8, 2);
    s_push(atlas, 3, 8, 8, 3);
    REQUIRE(bk__atlas_flush(atlas));
  }

  // One call does both: pass 1 dissolves the half-decayed atlas, and pass 3 -- the pack
  // loop -- must see survivors 1/2/3 as pending lonely candidates immediately, with no push
  // in between. That is only true if the dissolve actually cleared `atlassed` on them.
  REQUIRE(bk__atlas_defrag(atlas));

  REQUIRE(gpu.create_calls == create_calls_before_dissolve + 1); // one fresh atlas, in-call
  BK_AtlasEntry entry = {0};
  REQUIRE(bk__atlas_fetch(atlas, 1, &entry)); // resident with no intervening push
  s_require_rect_matches(&gpu, &entry);       // and the pixels actually landed there

  bk__atlas_destroy(atlas);
  s_free_gpu(&gpu);
}

// The harness itself must be able to see a leak (mutation discipline applied to the test):
// leak one block on purpose, observe, then free it.
static void test_leak_harness_detects(void) {
  BK_Allocator a = bk_allocator_counting(&s_atlas_counter);
  isize live_before = s_atlas_counter.live_allocs;
  void *leak = bk_alloc(&a, 64);
  REQUIRE(s_atlas_counter.live_allocs == live_before + 1);
  bk_free(&a, leak, 64);
  REQUIRE(s_atlas_counter.live_allocs == live_before);
}

static void test_atlas_rejects_partial_allocator(void) {
  FakeGpu gpu = {0};
  BK_AtlasDesc desc = s_make_desc(&gpu);
  desc.allocator.realloc_fn = nullptr; // now partial: alloc_fn/free_fn set, realloc_fn not
  BK_Atlas *atlas = bk__atlas_create(&desc);
  REQUIRE(atlas == nullptr);
}

int main(void) {
  test_flush_makes_pushed_images_resident();
  test_lonely_image_reports_its_whole_texture();
  test_repeated_pushes_reuse_one_texture();
  test_distinct_images_batch_separately_in_push_order();
  test_batch_order_follows_push_order_not_texture_handle_order();
  test_flush_drains_the_push_buffer();
  test_many_images_survive_index_growth();
  test_destroy_releases_every_texture();
  test_destroy_releases_atlas_textures();
  test_unavailable_image_is_dropped_and_the_frame_continues();
  test_failed_texture_creation_behaves_like_a_missing_image();
  test_a_dropped_image_is_retried_not_remembered();
  test_an_image_too_large_to_describe_is_dropped();
  test_a_dropped_image_can_come_back_at_a_different_size();
  test_a_failing_flush_still_drains_the_push_buffer();
  test_a_missing_images_failure_is_logged_once_per_lifetime();
  test_prefetch_makes_an_image_resident_without_a_push();
  test_prefetch_reports_an_unavailable_image();
  test_fetch_reports_residency_without_creating_it();
  test_invalidate_forgets_a_lonely_image();
  test_tick_ages_only_images_that_stop_being_pushed();
  test_flush_alone_never_builds_an_atlas();
  test_defrag_packs_lonely_images_into_one_atlas();
  test_an_image_looks_the_same_lonely_and_atlassed();
  test_oversized_images_stay_lonely_and_do_not_gate_the_threshold();
  test_images_that_do_not_fit_stay_pending();
  test_a_failed_atlas_leaves_every_image_drawable();
  test_a_get_pixels_failure_during_pack_does_not_corrupt_neighbors();
  test_a_pack_where_every_image_fails_builds_nothing();
  test_defrag_evicts_what_stopped_being_drawn();
  test_a_mostly_decayed_atlas_is_dissolved_and_repacked();
  test_a_lightly_decayed_atlas_is_left_alone();
  test_defrag_dissolves_every_fully_decayed_atlas();
  test_invalidating_an_atlassed_image_dissolves_its_atlas();
  test_dissolved_survivors_repack_within_the_same_defrag();
  test_leak_harness_detects();
  test_atlas_rejects_partial_allocator();

  // Every test above created and destroyed its atlas through the counting allocator;
  // anything still live is a leak in bk_atlas.c.
  REQUIRE_EQ_U64((u64)s_atlas_counter.live_bytes, 0);
  REQUIRE_EQ_U64((u64)s_atlas_counter.live_allocs, 0);
  REQUIRE(s_atlas_counter.total_allocs > 0);

  printf("test_atlas: OK\n");
  return 0;
}
