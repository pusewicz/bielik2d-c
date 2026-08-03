#include "bk_test.h"
#include "internal/bk_atlas_internal.h"

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
  if (gpu->fail_get_pixels && image_id == gpu->fail_get_pixels_image) {
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

int main(void) {
  test_flush_makes_pushed_images_resident();
  test_lonely_image_reports_its_whole_texture();
  test_repeated_pushes_reuse_one_texture();
  test_distinct_images_batch_separately_in_push_order();
  test_batch_order_follows_push_order_not_texture_handle_order();
  test_flush_drains_the_push_buffer();
  test_many_images_survive_index_growth();
  test_destroy_releases_every_texture();
  printf("test_atlas: OK\n");
  return 0;
}
