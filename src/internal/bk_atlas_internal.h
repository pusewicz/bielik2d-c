#pragma once
#include <bielik/bk_types.h>

/// A runtime atlas cache. Owns its residency state across frames; see bk__atlas_flush
/// for the per-frame cycle.
typedef struct BK_Atlas BK_Atlas;

/// One image referenced by the frame being built. The caller fills image_id, udata, width
/// and height; the cache overwrites texture_id and the source rect before handing the
/// entry back through submit_batch. Fields the caller sets are never modified.
typedef struct BK_AtlasEntry {
  u64 image_id;   // the caller's unique id for this image; the cache never interprets it
  u64 texture_id; // out -- whatever create_texture returned for the owning texture
  u64 udata;      // opaque; the cache never reads it. Correlates entries to caller data.
  i32 width, height;
  i32 min_x, min_y, max_x, max_y; // out -- texel rect within texture_id; see §3.3
} BK_AtlasEntry;

/// Writes image_id's pixels into buffer as tightly packed, premultiplied RGBA8, exactly
/// size bytes, where size == width * height * 4. Rows run top to bottom. width and height
/// are the values the cache holds for this image, passed so the callee can check them
/// against its own record rather than trusting size alone. Returns false if the image is
/// unavailable; the cache then drops the entry rather than uploading uninitialized memory.
/// Called from both bk__atlas_flush and bk__atlas_defrag.
typedef bool (*BK_AtlasGetPixelsFn)(u64 image_id, void *buffer, i32 size, i32 width, i32 height,
                                    void *udata);

/// Creates a texture from tightly packed RGBA8 pixels and returns an opaque handle, or 0
/// on failure. The cache only stores, compares and hands back this value -- see §3.2 for
/// what it may legally hold.
typedef u64 (*BK_AtlasCreateTextureFn)(const void *pixels, i32 width, i32 height, void *udata);

/// Destroys a handle previously returned by create_texture.
typedef void (*BK_AtlasDestroyTextureFn)(u64 texture_id, void *udata);

/// Reports one batch. Every entry in it shares texture_id, and entries appear in the order
/// they were pushed. Called during bk__atlas_flush, once per batch; the array is only
/// valid for the duration of the call. §4.2 covers the order batches arrive in, and why it
/// is not the caller's paint order.
///
/// Must not call back into this cache. bk__atlas_push in particular is forbidden and
/// asserts (§4.1); pushing here would write into the buffer flush is still draining.
typedef void (*BK_AtlasSubmitBatchFn)(const BK_AtlasEntry *entries, i32 count, void *udata);

typedef struct BK_AtlasDesc {
  BK_AtlasGetPixelsFn get_pixels;           // required
  BK_AtlasCreateTextureFn create_texture;   // required
  BK_AtlasDestroyTextureFn destroy_texture; // required
  BK_AtlasSubmitBatchFn submit_batch;       // required
  void *udata;                              // passed verbatim to every callback

  i32 atlas_size;              // 0 => 2048. Atlases are square, atlas_size x atlas_size.
  i32 ticks_until_decay;       // 0 => 1800. Entries unseen this many ticks are evictable.
  i32 defrag_lonely_threshold; // 0 => 16. bk__atlas_defrag packs pending lonely images
                               // into a new atlas while more than this many exist.
                               // Nothing packs without a defrag call; see §4.3.
} BK_AtlasDesc;

/// Creates a cache. Every callback in desc must be non-null (BK_ASSERT). Returns nullptr
/// and logs via SDL_Log on allocation failure.
[[nodiscard]] BK_Atlas *bk__atlas_create(const BK_AtlasDesc *desc);

/// Destroys the cache, calling destroy_texture for every texture it still owns. No-op if
/// atlas is nullptr.
void bk__atlas_destroy(BK_Atlas *atlas);

/// Buffers one entry for the frame being built. Does no packing, no uploading and no
/// callbacks -- all of that happens in flush, which is also the only thing that empties
/// this buffer (§4.1).
void bk__atlas_push(BK_Atlas *atlas, BK_AtlasEntry entry);

/// Makes image_id resident immediately -- calling get_pixels and create_texture before it
/// returns -- so a later push finds it already there. Use before an animation starts
/// rather than paying the upload on its first visible frame. Returns false if the image
/// could not be made resident; that is logged, not cached, and a later push retries.
/// No-op returning true if image_id is already resident. Deliberately not [[nodiscard]],
/// unlike the calls below it: a failed prefetch costs only the optimization, and the push
/// that follows reports the same failure through flush.
bool bk__atlas_prefetch(BK_Atlas *atlas, u64 image_id, i32 width, i32 height);

/// Forgets image_id entirely, so the next push re-fetches its pixels at whatever size that
/// push carries. If it is lonely, its texture is destroyed. If it lives in an atlas, that
/// whole atlas is dissolved -- which invalidates the rects previously reported for every
/// image in it, and is why frequently updated images are better left lonely (raise
/// defrag_lonely_threshold if that is the workload). The dissolved neighbours keep their
/// records and re-upload on their next push; only image_id's record is dropped. Never
/// fails: it only destroys and forgets. No-op if image_id is not resident.
void bk__atlas_invalidate(BK_Atlas *atlas, u64 image_id);

/// Looks up a resident image's texture and rect without pushing it, for drawing through
/// something outside this cache. Returns false and leaves *out untouched if image_id is
/// not currently resident -- this call never makes anything resident, so pair it with
/// bk__atlas_prefetch. The result is invalidated by the next defrag or invalidate.
[[nodiscard]] bool bk__atlas_fetch(BK_Atlas *atlas, u64 image_id, BK_AtlasEntry *out);

/// Advances the LRU clock by one. Call once per frame; §5.3 covers who calls it and what
/// an idle frame means.
void bk__atlas_tick(BK_Atlas *atlas);

/// Makes every pushed entry resident, sorts them, and reports them through submit_batch.
/// Always empties the push buffer. Returns false if the frame was reported incompletely --
/// see §4.1 for exactly what that means and what holds either way.
[[nodiscard]] bool bk__atlas_flush(BK_Atlas *atlas);

/// Packs accumulated lonely images into atlases, and dissolves atlases whose occupancy has
/// fallen. This is the only thing that ever creates an atlas (§4.3). Invalidates every rect
/// previously reported, so run it between frames, never between a push and its flush.
/// Returns false and logs if any step failed; the cache stays usable either way.
[[nodiscard]] bool bk__atlas_defrag(BK_Atlas *atlas);
