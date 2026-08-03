# Bielik2D — Phase 3, Sub-project 4: `bk_atlas`

## 0. Context and scope

P3.3 landed `bk_draw`, where every distinct texture splits a batch. That is the gap this
sub-project closes: a runtime atlas cache packs many small images into shared textures, so
sprites drawn from different source images collapse into one instanced draw instead of one
draw each.

P3.4 ships the cache **and nothing else** — no sprite type, no animation, no `bk_draw`
integration, no caller at all. Those are P3.5.

Phase 3's remaining sequence, renumbered because P3.4 was split in two:

| | | Status |
|---|---|---|
| P3.1 | `bk_math` | landed, PR #20 |
| P3.2 | gfx substrate | landed, PR #23 |
| P3.3 | `bk_draw` — unified SDF renderer | landed, PR #43 |
| **P3.4** | **`bk_atlas` — runtime residency cache** | **this spec** |
| P3.5 | `BK_Sprite`, animation, `bk_draw_sprite`, 9-slice | |
| P3.6 | wide shapes — polyline, polygon, bezier, CSG, custom SDFs | was P3.5 |
| P3.7 | tiled compute path | was P3.6 |

### 0.1 What it is, and what it is not

Ported from Cute Framework's `libraries/cute/cute_atlas_cache.h` (zlib/public domain).

It is **not** a packer you add images to and get permanent handles from. It is a per-frame
residency cache:

1. Each frame the owner **pushes** the entries it is about to draw.
2. **Flush** makes sure their pixels are resident on the GPU, sorts them, and reports them
   back as **batches** — each batch sharing one texture, with source rects filled in.
3. Images that stop being pushed **decay**, and **defrag** later reclaims their space.

Three donor concepts, kept:

- **Lonely textures.** An image gets its own texture rather than an atlas slot when it is
  new, or too large to pack usefully. Being lonely is temporary for the former and
  permanent for the latter (§4.3).
- **Ticks and decay.** `tick` advances an LRU clock. An entry not seen for
  `ticks_until_decay` becomes an eviction candidate.
- **Defrag.** Builds atlases out of accumulated lonely images, and dissolves atlases whose
  occupancy has dropped, reclaiming the holes left by decayed images.

**Atlases are built only by `defrag`.** Flush never creates one. This is the donor's
structure and it is easy to misread — see §4.3 and §5.3, which exist because of it.

## 1. File layout

```
bielik2d/
  src/
    bk_atlas.c                 (new; the whole cache)
    internal/
      bk_atlas_internal.h      (new; §3)
  tests/
    test_atlas.c               (new; CPU-only, CI-required)
  NOTICE.md                    (edit: cute_atlas_cache.h attribution)
  CMakeLists.txt               (edit: add src/bk_atlas.c to the bielik target, line 33)
  tests/CMakeLists.txt         (edit: register test_atlas)
```

No public header, no sample, no `bk_gfx` dependency, no `bk_app` hook. §2 explains the
first; §5.3 the rest.

## 2. Decision: internal, not public

`src/internal/bk_atlas_internal.h`, `bk__atlas_*` prefix, `BK_Atlas*` types.

The only consumer is P3.5's sprite layer. A public header with no game-facing caller is the
speculative option `CLAUDE.md` forbids, and tests include internal headers freely
(`tests/test_draw.c` already does), so nothing is lost in coverage. Promoting it later — if
a game rendering through raw `bk_gfx` wants atlasing — is a rename and a file move, not a
redesign.

## 3. The API

```c
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
typedef bool (*BK_AtlasGetPixelsFn)(u64 image_id, void *buffer, i32 size, i32 width,
                                    i32 height, void *udata);

/// Creates a texture from tightly packed RGBA8 pixels and returns an opaque handle, or 0
/// on failure. The cache only stores, compares and hands back this value -- see §3.2 for
/// what it may legally hold.
typedef u64 (*BK_AtlasCreateTextureFn)(const void *pixels, i32 width, i32 height,
                                       void *udata);

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
```

### 3.1 Departures from the donor, all deliberate

- **`get_pixels` returns `bool`, and takes `width`/`height`.** CF's returns void, takes only
  a byte count, and assumes success. P3.3 shipped two separate bugs in this family — drawing
  from a buffer whose upload failed, and a frame dropped with nothing logged — so a callback
  that cannot produce pixels must be able to say so. The entry is dropped, not faked.
- **`fetch` is read-only, returns `bool`, and takes no `width`/`height`.** CF's `fetch`
  quietly *creates* a lonely texture for a non-resident image, which is why it needs the
  dimensions; it then signals "nothing found" with a zeroed struct a caller can mistake for
  a valid entry at rect (0,0,0,0). **The donor's own documentation does not describe this**:
  `cute_atlas_cache.h:249` says only that "if a match for `image_id` is found, the texture id
  and uv coordinates are looked up and returned as an entry" — nothing about uploading pixels
  and minting a texture when no match is found, which is what the body does. Here `prefetch`
  is the one entry point that makes things resident, and `fetch` only reports. Removing the
  dimensions removes the only reason they could disagree with the resident record.
- **Entries report a texel rect, not normalized UVs** — §3.3. This is the departure with the
  most downstream effect.
- **No per-flush re-creation of lonely textures.** CF's flush walks every lonely entry and
  re-creates any texture a dissolved atlas left missing. Here that happens lazily, only for
  images this frame actually pushed. An entry nobody draws any more decays instead of being
  re-uploaded first.
- **The atlas-decay test is `decayed / total >= 0.5`, stated positively** — §4.4. The donor's
  is inverted and does not do what its own comment says; do not port it back.

**Deferred:** `atlas_cache_register_premade_atlas`. It is the bridge to offline-packed
atlases, and the offline pipeline does not exist until P6. No consumer, so it waits.

### 3.2 What `texture_id` may hold — normative

The cache treats `texture_id` as opaque: it stores it, compares it for equality, groups
batches by it, and passes it to `destroy_texture`. It never dereferences it and never
assumes any structure.

**It must be stable for the lifetime of the texture**, from the `create_texture` that
returned it until the `destroy_texture` that takes it. Both of these are legal:

- **A pointer cast through `u64`.** This is what P3.5 will do — `bk_gfx_bind_texture` takes
  a `BK_GfxTexture *`, so the callbacks cast to and from it. The caller owns the object's
  lifetime and must not free it behind the cache's back.
- **An index or generation-counted handle into a caller-side table.** This is what the
  tests do, minting monotonic integers from 1.

**0 is reserved** and means "no texture" — `create_texture` returns it on failure, and the
cache never reports an entry carrying it.

Written down because P3.3's implementer had to invent an equivalent contract (the
`meta[2]`/`meta[3]` payload indices) that its spec left unspecified, and that contract then
became load-bearing for a shader author who could not see it.

### 3.3 What the reported rect means — normative

`min_x, min_y, max_x, max_y` are **texels in `texture_id`, top-left origin, `max` exclusive**,
so `max_x - min_x == width` and `max_y - min_y == height` always hold. A lonely image
therefore reports exactly `(0, 0, width, height)`.

This is the same space and orientation `bk_draw_texture` already documents for its `src_px`
argument, so P3.5 passes the rect straight through with no conversion and no scaling by a
texture size the entry does not carry.

**There is no y-flip anywhere in this module, and an image reports the same orientation
whether it is lonely or atlassed.** The donor flips v on the lonely path only, gated by a
compile-time macro (`ATLAS_CACHE_LONELY_FLIP_Y_AXIS_FOR_UV`), so the same image samples
differently depending on where it happens to live. Porting that would make a sprite flip on
the frame its image graduated into an atlas — silent, visual, and untraceable. Reporting
texels instead of UVs removes the choice rather than documenting it: `bk_draw` already owns
the single y-flip, in `s_write_shape_payload`, and it needs no help from here. §5.1 asserts
the two orientations agree.

## 4. Lifetimes, ordering and failure

### 4.1 The push buffer, and what a `false` flush means

**The push buffer must not live in the frame arena.** `bk_frame_alloc` is the obvious
choice and the wrong one: `bk__arena_reset` runs unconditionally every frame, while the
cache's residency state — atlases, entries, the LRU — is inherently multi-frame. The cache
owns its own allocations via `SDL_malloc`/`SDL_realloc`, freed in `bk__atlas_destroy`.

Pushes accumulate until a flush drains them; there is no other frame boundary and nothing
else clears the buffer. **`bk__atlas_flush` snapshots and clears it before anything that
can fail** — same discipline P3.3 §5 imposed on the draw chain, and for the same reason: a
failure partway through must not leave entries buffered to replay against a frame that
never happened.

Flush reads that buffer in place while it reports batches, so **`bk__atlas_push` is
forbidden from inside `submit_batch` and asserts.** A push there would either overwrite
entries not yet reported or grow the buffer out from under the pointer flush is holding —
the second is a use-after-free, and it is the same one P3.3 shipped twice for the same
reason. The prohibition costs the caller nothing: a batch handler draws, it does not
enqueue more work for the frame it is already in the middle of.

An image that cannot be made resident is **dropped, and the frame continues**. There is one
rule for both ways that can happen — `get_pixels` returning false, or `create_texture`
returning 0 — because they mean the same thing to the cache. Each is logged via `SDL_Log`
with a `"BK: "` prefix, deduplicated per `image_id` so a permanently missing image does not
spam a frame loop. The failure is **not** remembered: a later push retries.

`bk__atlas_flush` returns **false when the frame was reported incompletely** — at least one
entry was dropped, or an internal allocation failed. It is not "the frame was abandoned".
These hold whichever value it returns:

- The push buffer is empty.
- Every surviving entry was reported in exactly one batch, including entries pushed after
  the one that failed.
- No reported entry carries `texture_id == 0`, and no residency record points at a texture
  that was not created.

Dropping an entry is safe because the caller re-pushes every frame — that is the model. A
dropped image renders nothing that frame; it does not corrupt the next one.

### 4.2 Sorting, and why batch order is not paint order

Entries are grouped by texture, so images sharing one land in a single batch. Two ordering
rules, both testable:

- **Within a batch, entries appear in push order.** The sort is stable, with push index as
  the tiebreaker.
- **Batches arrive ordered by their earliest-pushed entry.** Not by `texture_id` value —
  handles come from `create_texture` and sorting on them would make batch order depend on
  allocation history, and make the tests depend on the fake's numbering.

**This is still not the caller's paint order.** Grouping by texture necessarily hoists a
late entry forward when it shares a texture with an early one. If P3.5 issues `bk_draw`
calls straight from `submit_batch`, two overlapping sprites on different textures composite
in the wrong order. Restoring paint order is the caller's job, and `udata` is what it
carries to do so — `bk_draw` already sorts by `(layer, record_id)`, so P3.5 has a place to
put it. Stated here rather than left implicit, because a silent z-order inversion is exactly
the kind of bug that survives a green test suite.

P3.3 shipped a stable-sort test that passed under an anti-stable sort because its fixture
was palindromic; §5.1 says how this one avoids that.

### 4.3 Lonely, atlassed, and who builds atlases

- An image whose width **or** height exceeds `atlas_size / 2` is **permanently lonely**: it
  can never pack usefully, so it gets its own texture for as long as it is resident. It may
  exceed `atlas_size` outright — nothing here tries to pack it, so there is no upper bound
  beyond what `create_texture` accepts. **Permanently lonely images are never packing
  candidates and never count toward `defrag_lonely_threshold`** — counting them would let a
  handful of oversized images hold the threshold above its trigger forever, so nothing else
  ever packs either.
- Every other new image is **temporarily lonely**: its first residency is its own texture,
  created on first push.

**`bk__atlas_defrag` is the only thing that builds an atlas.** While more than
`defrag_lonely_threshold` temporarily-lonely images are pending, defrag packs as many as fit
into one new atlas texture, destroys their individual textures, and records each one's texel
rect. A cache that is flushed and ticked but never defragged works correctly and leaves
every image lonely forever — one draw call each, which is the thing this module exists to
avoid.

**Packing is shelf packing, over candidates sorted by `(last_tick` descending, `height`
descending`)`.** Most-recently-seen first is what puts images drawn together into one atlas,
which is the whole point; height descending is what makes shelf packing behave. As one total
order they cooperate rather than compete: everything pushed in the same frame shares a
`last_tick`, so within a frame the order *is* tallest-first, and older images simply queue
behind. Place each candidate into the current row, start a new row when the row runs out of
width, and stop when the next row would exceed `atlas_size`. Whatever did not fit stays
pending for the next pack. Named here rather than left to the implementer because "as many
as fit" has no single answer, and the tests need a deterministic one.

Deferring the pack is what stops a one-frame image from forcing an atlas build. The
threshold is a number rather than a heuristic so that a test can drive it exactly. It is 16
rather than the donor's 64 because each pending lonely image costs one batch in `bk_draw`,
and 64 extra draw calls is a worse steady state than a slightly more eager pack.

Residency is keyed on `image_id` alone. Pushing one `image_id` with different `width`/
`height` than the resident record is a caller error (`BK_ASSERT`); call
`bk__atlas_invalidate` first — it drops the record outright, so the next push adopts the new
size.

`image_id` has no reserved value: 0 is as valid an id as any other. The residency index is
keyed on the value alone and uses a separate empty marker, so nothing here forces the caller
to keep an id free.

### 4.4 Decay and defrag

`tick` advances a monotonic counter. Each resident entry records the tick it was last
pushed. An entry whose age exceeds `ticks_until_decay` (default 1800 — 30 seconds at 60 Hz,
the donor's value) is evictable, but is not evicted eagerly. `defrag` does the work, in this
order:

1. **Dissolve decayed atlases.** For each atlas, count how many of its entries have decayed.
   If `decayed / total >= 0.5`, destroy the atlas texture, drop the decayed entries entirely,
   and return the survivors to the pending-lonely set with no texture — the next push of one
   creates its texture again, and step 3 may re-pack it in this same call.
2. **Destroy decayed lonely textures**, and forget their entries.
3. **Pack.** While more than `defrag_lonely_threshold` pending lonely images remain, build
   one atlas from as many as fit (§4.3): call `get_pixels` for each image being packed, blit
   it into the atlas image at its shelf position, create the atlas texture, then destroy each
   packed image's own texture — *if it has one*, since step 1 returns survivors with none —
   and record its rect. Stop early if a pack places nothing, so images that cannot fit cannot
   spin. An image whose `get_pixels` fails is dropped by the §4.1 rule and leaves the pack.

Step 1's threshold is stated positively on purpose. The donor computes
`total / decayed` and dissolves when that exceeds `ratio_to_decay_atlas` (default `0.5`) —
a ratio that is `>= 1` whenever anything has decayed at all, so the donor in fact dissolves
an atlas the moment a single entry decays, which is not what its own comment describes. Port
the intent, not the arithmetic.

**`defrag` invalidates every rect previously reported.** It must run between frames, never
between a push and its flush. The header says so; §5.1 tests it.

## 5. Testing

### 5.1 `tests/test_atlas.c` — CPU only, CI-required on every platform

This is the point of the callback design. The cache needs no GPU, so unlike P3.3's subtlest
logic — which sits in `test_draw_gpu` under `continue-on-error` on every CI leg — all of
this is gated everywhere.

Fakes: `get_pixels` fills a pattern derived from `image_id` (so a pixel identifies its
source image); `create_texture` mints monotonic ids from 1 and **keeps a copy of the pixels
it was handed**; `destroy_texture` records the id; `submit_batch` appends
`(texture_id, udata)` pairs to a log.

Coverage:

- entries sharing an atlas report as one batch, and entries on different textures as several
- an image larger than `atlas_size / 2` in either dimension stays lonely across a defrag
  that packs its smaller neighbours, and does not count toward the threshold: with
  `defrag_lonely_threshold` oversized images pending, adding enough small ones still packs
- flush alone never creates an atlas, however many images are pushed — only defrag does
- images that do not fit the atlas being built stay pending, and a later defrag packs them
- defrag packs pending lonely images once more than `defrag_lonely_threshold` exist, and
  destroys their individual textures
- **the same image reports the same content lonely and atlassed**: read the recorded pixels
  at the reported rect, before and after the defrag that packs it, and assert both match the
  pattern `get_pixels` wrote — the orientation and packing guard from §3.3, and the reason
  the fake keeps pixels at all
- a lonely image reports exactly `(0, 0, width, height)`
- an entry unseen for `ticks_until_decay` is evicted by defrag; one still being pushed
  survives it
- an atlas at or past half decayed is dissolved and its survivors are re-packed; one below
  half is left alone
- `invalidate` on a lonely image destroys its texture and re-fetches pixels on the next push
- `invalidate` on an atlassed image dissolves that atlas, and its neighbours re-fetch too
- `get_pixels` returning false drops that entry, creates no texture, makes flush return
  false, and still reports every other entry in the frame
- a failed `create_texture` behaves identically, and no reported entry carries `texture_id`
  of 0
- `fetch` on a non-resident image returns false and does not write `*out`; `prefetch` then
  `fetch` returns true without any push
- push order survives within a batch, and batches arrive ordered by earliest-pushed entry

**Batch assertions check which entries landed where, by `udata` — never batch counts
alone.** A fake minting monotonic texture ids makes `batch_count == 2` pass under a bug that
gives every entry its own texture, and under one that reports batches in the wrong order.
This is precisely the failure mode that let four P3.3 tests pass under the exact regressions
they named.

The order tests use a `udata` sequence that is **not** palindromic, for the same reason.

### 5.2 Mutation verification is part of each task, not an afterthought

Every test that guards a specific behaviour is verified by breaking that behaviour and
confirming the test fails at the expected assertion. P3.3's record — four tests that passed
under the regression they named — makes this non-optional. Prefer a different bug class for
the second check on any given guard: repeating one mutation only re-proves what is already
known.

### 5.3 No caller, and what P3.5 owes

P3.4 ships no caller, so two cadences are unowned and P3.5 owns both:

- **`bk__atlas_tick`** — once per frame, from the same place it flushes. An idle frame that
  draws nothing still ticks, so entries age while a scene is paused; that is what makes
  decay work for a game that switches scenes.
- **`bk__atlas_defrag`** — every N flushes, between frames. Without it nothing is ever
  packed and the module does nothing but add a layer of indirection (§4.3). It must not run
  between a push and its flush.

Stated because P3.3's spec omitted where `bk__draw_collate` was called from, and that
omission cost a fix round. Here the answer is "nowhere yet, and P3.5 owes it" — a different
answer from silence.

No sample either: there is nothing to see until sprites exist.

## 6. Decisions and rationale (do not relitigate in implementation sessions)

- **Callbacks, not a direct `bk_gfx` dependency.** Chosen so the whole cache is CPU-testable
  and CI-required. The alternative put eviction and defrag — the subtlest logic here —
  behind a GPU requirement, which on this project means the allow-failure group.
- **Internal, not public.** §2.
- **Full LRU and defrag, not a no-eviction shelf packer.** Deliberate: the simpler design
  would have made handles permanent and the API trivial, at the cost of an unbounded working
  set. Chosen with that trade understood.
- **Texel rects, not UVs.** §3.3. This is what makes the donor's conditional y-flip a
  non-question.
- **One failure rule for unavailable images: drop and continue.** §4.1. `get_pixels`
  failing and `create_texture` failing mean the same thing to the cache, so they behave the
  same way; `false` from flush reports incompleteness, not abandonment.
- **The push buffer is not arena-allocated.** §4.1. Recorded because `bk_frame_alloc` is the
  obvious wrong answer.
- **`register_premade_atlas` deferred to P6.** §3.1.
- **Attribution.** `PLAN.md` §7's zlib requirement covers this port too;
  `cute_atlas_cache.h` joins the `NOTICE.md` entry `bk_draw` added.

## 7. Explicitly out of scope

- **`BK_Sprite`, animation, `bk_draw_sprite`, 9-slice** — P3.5.
- **Any `bk_draw` or `bk_gfx` integration** — P3.5 supplies the real callbacks, and owes the
  `tick` and `defrag` cadences (§5.3) and the paint-order restoration (§4.2).
- **Border pixels between packed images** — issue #45. Costs a packer change, a rect change
  and a test, with no sampler and no caller here to bleed. P3.5 adds it before any sprite art
  goes through the atlas path.
- **Merging mostly-empty atlases** — issue #46. The donor's second defrag pass; an
  optimization on top of decay-and-recompile, with no measurement yet to justify it.
- **Image decoding and file I/O** — P6. The cache never sees a filename.
- **Premade/offline atlases** — P6.
- **A public header or a sample** — §2, §5.3.
