#include "internal/bk_alloc_internal.h"
#include "internal/bk_atlas_internal.h"

#include <bielik/bk_app.h>
#include <bielik/bk_types.h>

#include <SDL3/SDL.h>

constexpr i32 BK_ATLAS_DEFAULT_SIZE = 2048;
constexpr i32 BK_ATLAS_DEFAULT_TICKS_UNTIL_DECAY = 1800;
constexpr i32 BK_ATLAS_DEFAULT_LONELY_THRESHOLD = 16;

/// One resident image. texture_id is 0 when the image is known but has no texture yet --
/// the state a defrag leaves behind when it dissolves the atlas an image was living on.
///
/// A record is always in exactly one of four states: lonely-with-texture (!atlassed,
/// texture_id != 0 -- s_make_resident's starting state, owning texture_id alone), atlassed
/// (atlassed == true -- shares texture_id with others, min_x/min_y meaningful), textureless
/// survivor (!atlassed, texture_id == 0 -- what a dissolve leaves a non-decayed neighbour
/// in, until its next push re-uploads it), or permanently lonely (permanent_lonely == true
/// -- pinned to the first or third state forever, since s_pack_one_atlas never candidates
/// it; spec section 4.3).
typedef struct BK_AtlasRecord {
  u64 image_id;
  u64 texture_id;
  i32 width, height;
  i32 min_x, min_y; // texel origin within texture_id; (0,0) while lonely
  i32 last_tick;
  bool atlassed;         // false => this record owns texture_id alone
  bool permanent_lonely; // too large to ever pack (spec section 4.3)
} BK_AtlasRecord;

/// Open-addressed image_id -> record index. Linear probing, power-of-two capacity, and no
/// tombstones: removals swap-remove from the record array and rebuild the whole table.
/// Removals only happen in defrag and invalidate, so the rebuild is off the hot path, and
/// avoiding tombstones removes the only way this table can silently degrade.
///
/// values[slot] == -1 marks an empty slot, so no image_id value is reserved.
typedef struct BK_AtlasMap {
  u64 *keys;
  i32 *values;
  i32 capacity; // power of two, or 0 when unallocated
} BK_AtlasMap;

struct BK_Atlas {
  BK_AtlasDesc desc;

  BK_AtlasRecord *records;
  i32 record_count, record_capacity;
  BK_AtlasMap map;

  u64 *atlas_textures; // one handle per live atlas
  i32 atlas_count, atlas_capacity;

  BK_AtlasEntry *pushed;
  i32 push_count, push_capacity;
  bool in_flush;

  BK_AtlasEntry *flush_batch; // scratch for one submit_batch call, grown across flushes
  i32 flush_batch_capacity;

  u64 *logged_failures; // image_ids already logged, so a missing image logs once
  i32 logged_count, logged_capacity;

  i32 tick;
};

/// Hash is the splitmix64 finalizer -- cheap and it scatters the low bits that sequential
/// image_ids share.
static u64 s_hash_u64(u64 key) {
  key ^= key >> 30;
  key *= 0xBF58476D1CE4E5B9u;
  key ^= key >> 27;
  key *= 0x94D049BB133111EBu;
  key ^= key >> 31;
  return key;
}

/// Inserts into an already-sized table. Callers guarantee a free slot exists.
static void s_map_put(BK_AtlasMap *map, u64 key, i32 value) {
  i32 mask = map->capacity - 1;
  i32 slot = (i32)(s_hash_u64(key) & (u64)mask);
  while (map->values[slot] != -1) {
    if (map->keys[slot] == key) {
      map->values[slot] = value;
      return;
    }
    slot = (slot + 1) & mask;
  }
  map->keys[slot] = key;
  map->values[slot] = value;
}

/// Record index for image_id, or -1.
static i32 s_map_get(const BK_AtlasMap *map, u64 key) {
  if (map->capacity == 0) {
    return -1;
  }
  i32 mask = map->capacity - 1;
  i32 slot = (i32)(s_hash_u64(key) & (u64)mask);
  while (map->values[slot] != -1) {
    if (map->keys[slot] == key) {
      return map->values[slot];
    }
    slot = (slot + 1) & mask;
  }
  return -1;
}

/// Re-points every slot at the record array as it stands now. Allocates nothing, so it
/// cannot fail -- which is what makes removal safe: a removal never needs more capacity,
/// and a removal that could fail would leave the table indexing a record array that has
/// already shifted underneath it.
static void s_map_reindex(BK_Atlas *atlas) {
  if (atlas->map.capacity == 0) {
    return;
  }
  for (i32 i = 0; i < atlas->map.capacity; i++) {
    atlas->map.values[i] = -1;
  }
  for (i32 i = 0; i < atlas->record_count; i++) {
    s_map_put(&atlas->map, atlas->records[i].image_id, i);
  }
}

/// Grows the table and reindexes. Growth only, so only s_record_add calls this. Always
/// succeeds: the allocator seam aborts rather than returning failure here.
static bool s_map_rebuild(BK_Atlas *atlas) {
  i32 wanted = 16;
  while (wanted * 3 < (atlas->record_count + 1) * 4) {
    wanted *= 2;
  }
  u64 *keys =
      BK__ALLOC_ZERO(&atlas->desc.allocator, BK_MEM_TAG_ATLAS, (isize)wanted * (isize)sizeof(u64));
  i32 *values =
      BK__ALLOC(&atlas->desc.allocator, BK_MEM_TAG_ATLAS, (isize)wanted * (isize)sizeof(i32));
  i32 old_capacity = atlas->map.capacity;
  BK__FREE(&atlas->desc.allocator, BK_MEM_TAG_ATLAS, atlas->map.keys,
           (isize)old_capacity * (isize)sizeof(u64));
  BK__FREE(&atlas->desc.allocator, BK_MEM_TAG_ATLAS, atlas->map.values,
           (isize)old_capacity * (isize)sizeof(i32));
  atlas->map = (BK_AtlasMap){.keys = keys, .values = values, .capacity = wanted};
  s_map_reindex(atlas);
  return true;
}

/// Appends a record and indexes it. Returns the new record's index. The -1 return guards
/// against s_map_rebuild failing, which cannot happen under the allocator seam's
/// abort-on-OOM contract (see s_map_rebuild's own comment) -- kept as defense-in-depth, not
/// a path either call reaches today.
static i32 s_record_add(BK_Atlas *atlas, const BK_AtlasRecord *record) {
  if (atlas->record_count == atlas->record_capacity) {
    i32 wanted = atlas->record_capacity == 0 ? 16 : atlas->record_capacity * 2;
    atlas->records = BK__REALLOC(&atlas->desc.allocator, BK_MEM_TAG_ATLAS, atlas->records,
                                 (isize)atlas->record_capacity * (isize)sizeof(BK_AtlasRecord),
                                 (isize)wanted * (isize)sizeof(BK_AtlasRecord));
    atlas->record_capacity = wanted;
  }
  atlas->records[atlas->record_count] = *record;
  atlas->record_count++;
  if (atlas->map.capacity * 3 < atlas->record_count * 4) {
    if (!s_map_rebuild(atlas)) {
      atlas->record_count--;
      return -1;
    }
  } else {
    s_map_put(&atlas->map, record->image_id, atlas->record_count - 1);
  }
  return atlas->record_count - 1;
}

/// Removes the record at index. Does NOT destroy its texture -- callers decide that. The
/// swap-remove moves the last record into this slot, so any index held across this call is
/// stale; re-look-up rather than caching indices.
static void s_record_remove(BK_Atlas *atlas, i32 index) {
  BK_ASSERT(index >= 0 && index < atlas->record_count);
  atlas->records[index] = atlas->records[atlas->record_count - 1];
  atlas->record_count--;
  // s_map_reindex, not s_map_rebuild. The swap-remove has already happened, so a rebuild
  // that failed its allocation would leave the OLD table mapping the moved record's
  // image_id to an index past the new record_count -- an out-of-bounds read on the next
  // lookup, silent, and invisible to every test here because tests never fail a malloc.
  s_map_reindex(atlas);
}

BK_Atlas *bk__atlas_create(const BK_AtlasDesc *desc) {
  BK_ASSERT(desc != nullptr);
  BK_ASSERT(desc->get_pixels != nullptr);
  BK_ASSERT(desc->create_texture != nullptr);
  BK_ASSERT(desc->destroy_texture != nullptr);
  BK_ASSERT(desc->submit_batch != nullptr);

  if (!bk__allocator_valid(&desc->allocator)) {
    SDL_Log("BK: bk__atlas_create: allocator is partially set (all three functions or none)");
    return nullptr;
  }

  BK_Atlas *atlas = BK__ALLOC_ZERO(&desc->allocator, BK_MEM_TAG_ATLAS, (isize)sizeof(BK_Atlas));
  atlas->desc = *desc;
  if (atlas->desc.atlas_size <= 0) {
    atlas->desc.atlas_size = BK_ATLAS_DEFAULT_SIZE;
  }
  if (atlas->desc.ticks_until_decay <= 0) {
    atlas->desc.ticks_until_decay = BK_ATLAS_DEFAULT_TICKS_UNTIL_DECAY;
  }
  if (atlas->desc.defrag_lonely_threshold <= 0) {
    atlas->desc.defrag_lonely_threshold = BK_ATLAS_DEFAULT_LONELY_THRESHOLD;
  }
  return atlas;
}

void bk__atlas_destroy(BK_Atlas *atlas) {
  if (atlas == nullptr) {
    return;
  }
  for (i32 i = 0; i < atlas->atlas_count; i++) {
    atlas->desc.destroy_texture(atlas->atlas_textures[i], atlas->desc.udata);
  }
  for (i32 i = 0; i < atlas->record_count; i++) {
    // Atlassed records share their atlas's handle, already destroyed above.
    if (!atlas->records[i].atlassed && atlas->records[i].texture_id != 0) {
      atlas->desc.destroy_texture(atlas->records[i].texture_id, atlas->desc.udata);
    }
  }
  BK__FREE(&atlas->desc.allocator, BK_MEM_TAG_ATLAS, atlas->records,
           (isize)atlas->record_capacity * (isize)sizeof(BK_AtlasRecord));
  BK__FREE(&atlas->desc.allocator, BK_MEM_TAG_ATLAS, atlas->map.keys,
           (isize)atlas->map.capacity * (isize)sizeof(u64));
  BK__FREE(&atlas->desc.allocator, BK_MEM_TAG_ATLAS, atlas->map.values,
           (isize)atlas->map.capacity * (isize)sizeof(i32));
  BK__FREE(&atlas->desc.allocator, BK_MEM_TAG_ATLAS, atlas->atlas_textures,
           (isize)atlas->atlas_capacity * (isize)sizeof(u64));
  BK__FREE(&atlas->desc.allocator, BK_MEM_TAG_ATLAS, atlas->pushed,
           (isize)atlas->push_capacity * (isize)sizeof(BK_AtlasEntry));
  BK__FREE(&atlas->desc.allocator, BK_MEM_TAG_ATLAS, atlas->flush_batch,
           (isize)atlas->flush_batch_capacity * (isize)sizeof(BK_AtlasEntry));
  BK__FREE(&atlas->desc.allocator, BK_MEM_TAG_ATLAS, atlas->logged_failures,
           (isize)atlas->logged_capacity * (isize)sizeof(u64));
  // The allocator lives inside the block being freed, so take a stack copy before freeing
  // atlas itself -- freeing through &atlas->desc.allocator after this point would read
  // memory that is no longer valid.
  BK_Allocator allocator = atlas->desc.allocator;
  BK__FREE(&allocator, BK_MEM_TAG_ATLAS, atlas, (isize)sizeof(BK_Atlas));
}

/// Copies a record's identity and placement into a caller-facing entry. udata is left
/// alone: it is the caller's, and the record does not carry it.
static void s_fill_entry_from_record(const BK_AtlasRecord *record, BK_AtlasEntry *out) {
  out->image_id = record->image_id;
  out->texture_id = record->texture_id;
  out->width = record->width;
  out->height = record->height;
  out->min_x = record->min_x;
  out->min_y = record->min_y;
  out->max_x = record->min_x + record->width;
  out->max_y = record->min_y + record->height;
}

/// Logs image_id's unavailability once per cache lifetime. A missing asset in a frame loop
/// would otherwise print sixty times a second, which buries every other message.
static void s_log_image_failure(BK_Atlas *atlas, u64 image_id, const char *reason) {
  for (i32 i = 0; i < atlas->logged_count; i++) {
    if (atlas->logged_failures[i] == image_id) {
      return;
    }
  }
  SDL_Log("BK: bk_atlas: dropping image %llu: %s", (unsigned long long)image_id, reason);
  if (atlas->logged_count == atlas->logged_capacity) {
    i32 wanted = atlas->logged_capacity == 0 ? 8 : atlas->logged_capacity * 2;
    atlas->logged_failures = BK__REALLOC(
        &atlas->desc.allocator, BK_MEM_TAG_ATLAS, atlas->logged_failures,
        (isize)atlas->logged_capacity * (isize)sizeof(u64), (isize)wanted * (isize)sizeof(u64));
    atlas->logged_capacity = wanted;
  }
  atlas->logged_failures[atlas->logged_count++] = image_id;
}

/// Ensures image_id has pixels on a texture, creating a lonely texture if it does not.
/// Returns the record's index, or -1 if the image could not be made resident: an oversized
/// byte count, get_pixels returning false, or create_texture returning 0 (each logged via
/// s_log_image_failure) -- or, defensively, s_record_add failing, which the allocator seam's
/// abort-on-OOM contract no longer allows (see s_record_add's own comment). The index is
/// invalidated by any later s_record_remove -- do not hold it.
static i32 s_make_resident(BK_Atlas *atlas, u64 image_id, i32 width, i32 height) {
  BK_ASSERT(width > 0 && height > 0);

  i32 index = s_map_get(&atlas->map, image_id);
  if (index >= 0) {
    BK_AtlasRecord *found = &atlas->records[index];
    // Residency is keyed on image_id alone, so a size change behind the cache's back would
    // report a rect that does not match the pixels. bk__atlas_invalidate is the documented
    // way to resize an image (spec section 4.3). Asserted, not handled: there is no
    // sensible recovery, and an assert names the caller's bug at the call site.
    BK_ASSERT(found->width == width && found->height == height);
    found->last_tick = atlas->tick;
    if (found->texture_id != 0) {
      return index;
    }
    // texture_id == 0 means a defrag dissolved the atlas this image lived on. Its texture
    // comes back lazily, here, rather than eagerly for every survivor (spec section 3.1).
  } else {
    i32 half = atlas->desc.atlas_size / 2;
    BK_AtlasRecord fresh = {
        .image_id = image_id,
        .width = width,
        .height = height,
        .last_tick = atlas->tick,
        .permanent_lonely = width > half || height > half,
    };
    index = s_record_add(atlas, &fresh);
    if (index < 0) {
      return -1; // defense-in-depth, not reachable today -- see s_record_add's own comment
    }
  }

  usize bytes = (usize)width * (usize)height * 4u;
  // BK_AtlasGetPixelsFn takes size as i32, so an image whose byte count does not fit one
  // cannot be described to the callback at all. Reject it here instead of handing over a
  // truncated or negative size: permanently-lonely images have no upper dimension bound
  // (spec section 4.3), so this is reachable rather than theoretical. s_make_resident is
  // the only path that creates a record, so guarding here also guarantees the pack path
  // never sees an image whose bytes overflow.
  if (bytes > (usize)INT32_MAX) {
    s_log_image_failure(atlas, image_id, "image byte count exceeds INT32_MAX");
    s_record_remove(atlas, index);
    return -1;
  }
  void *pixels = BK__ALLOC(&atlas->desc.allocator, BK_MEM_TAG_ATLAS, (isize)bytes);
  if (!atlas->desc.get_pixels(image_id, pixels, (i32)bytes, width, height, atlas->desc.udata)) {
    BK__FREE(&atlas->desc.allocator, BK_MEM_TAG_ATLAS, pixels, (isize)bytes);
    s_log_image_failure(atlas, image_id, "get_pixels returned false");
    s_record_remove(atlas, index);
    return -1;
  }
  u64 texture_id = atlas->desc.create_texture(pixels, width, height, atlas->desc.udata);
  BK__FREE(&atlas->desc.allocator, BK_MEM_TAG_ATLAS, pixels, (isize)bytes);
  if (texture_id == 0) {
    s_log_image_failure(atlas, image_id, "create_texture returned 0");
    s_record_remove(atlas, index);
    return -1;
  }
  atlas->records[index].texture_id = texture_id;
  atlas->records[index].atlassed = false;
  atlas->records[index].min_x = 0;
  atlas->records[index].min_y = 0;
  return index;
}

void bk__atlas_push(BK_Atlas *atlas, BK_AtlasEntry entry) {
  BK_ASSERT(atlas != nullptr);
  // Forbidden from inside submit_batch: flush is still reading this buffer, so a push
  // here either overwrites entries not yet reported or reallocs out from under the
  // pointer flush holds (spec section 4.1).
  BK_ASSERT(!atlas->in_flush);

  if (atlas->push_count == atlas->push_capacity) {
    i32 wanted = atlas->push_capacity == 0 ? 16 : atlas->push_capacity * 2;
    atlas->pushed = BK__REALLOC(&atlas->desc.allocator, BK_MEM_TAG_ATLAS, atlas->pushed,
                                (isize)atlas->push_capacity * (isize)sizeof(BK_AtlasEntry),
                                (isize)wanted * (isize)sizeof(BK_AtlasEntry));
    atlas->push_capacity = wanted;
  }
  atlas->pushed[atlas->push_count] = entry;
  atlas->push_count++;
}

bool bk__atlas_prefetch(BK_Atlas *atlas, u64 image_id, i32 width, i32 height) {
  BK_ASSERT(atlas != nullptr);
  return s_make_resident(atlas, image_id, width, height) >= 0;
}

/// True when this record has not been pushed for ticks_until_decay ticks.
static bool s_is_decayed(const BK_Atlas *atlas, const BK_AtlasRecord *record) {
  return atlas->tick - record->last_tick >= atlas->desc.ticks_until_decay;
}

/// Destroys an atlas texture and returns its live images to the pending-lonely set with no
/// texture. Their pixels come back lazily, on their next push. Decayed images on it are
/// dropped outright.
static void s_dissolve_atlas(BK_Atlas *atlas, u64 texture_id, bool drop_decayed) {
  atlas->desc.destroy_texture(texture_id, atlas->desc.udata);
  for (i32 i = 0; i < atlas->atlas_count; i++) {
    if (atlas->atlas_textures[i] == texture_id) {
      atlas->atlas_textures[i] = atlas->atlas_textures[atlas->atlas_count - 1];
      atlas->atlas_count--;
      break;
    }
  }
  // Walk backwards: s_record_remove swap-removes, so a forward walk would skip the record
  // it moves into the slot just vacated.
  for (i32 i = atlas->record_count - 1; i >= 0; i--) {
    BK_AtlasRecord *record = &atlas->records[i];
    if (!record->atlassed || record->texture_id != texture_id) {
      continue;
    }
    if (drop_decayed && s_is_decayed(atlas, record)) {
      s_record_remove(atlas, i);
      continue;
    }
    record->texture_id = 0; // no texture until the next push re-uploads it
    record->atlassed = false;
    record->min_x = 0;
    record->min_y = 0;
  }
}

void bk__atlas_invalidate(BK_Atlas *atlas, u64 image_id) {
  BK_ASSERT(atlas != nullptr);
  i32 index = s_map_get(&atlas->map, image_id);
  if (index < 0) {
    return;
  }
  if (atlas->records[index].atlassed) {
    // The pixels are baked into a shared texture, so there is nothing to replace short of
    // rebuilding it. Dissolving is cheaper than a recompile and reuses machinery that has
    // to exist anyway; the neighbours re-upload on their next push. Keep the decayed ones
    // here -- invalidate is not an eviction.
    s_dissolve_atlas(atlas, atlas->records[index].texture_id, false);
    // Defensive rather than load-bearing here: s_dissolve_atlas only reorders the record
    // array when it drops a decayed entry, and drop_decayed is false on this call, so
    // nothing moves today. Kept so this stays correct if that argument ever changes.
    index = s_map_get(&atlas->map, image_id);
    BK_ASSERT(index >= 0);
  } else if (atlas->records[index].texture_id != 0) {
    atlas->desc.destroy_texture(atlas->records[index].texture_id, atlas->desc.udata);
  }
  s_record_remove(atlas, index);
}

bool bk__atlas_fetch(BK_Atlas *atlas, u64 image_id, BK_AtlasEntry *out) {
  BK_ASSERT(atlas != nullptr);
  BK_ASSERT(out != nullptr);
  i32 index = s_map_get(&atlas->map, image_id);
  if (index < 0 || atlas->records[index].texture_id == 0) {
    return false; // *out is deliberately untouched (spec section 3.1)
  }
  s_fill_entry_from_record(&atlas->records[index], out);
  return true;
}

void bk__atlas_tick(BK_Atlas *atlas) {
  BK_ASSERT(atlas != nullptr);
  atlas->tick++;
}

/// A total order, so an unstable sort is fine: ties on group are broken by push index, and
/// push indices are unique. Sorting on texture_id instead would make batch order depend on
/// allocation history -- and make the tests depend on the fake's numbering (spec 4.2).
typedef struct BK_AtlasSortItem {
  i32 group;
  i32 index;
} BK_AtlasSortItem;

static int s_compare_sort_items(const void *lhs, const void *rhs) {
  const BK_AtlasSortItem *left = lhs;
  const BK_AtlasSortItem *right = rhs;
  if (left->group != right->group) {
    return left->group < right->group ? -1 : 1;
  }
  return left->index < right->index ? -1 : 1;
}

bool bk__atlas_flush(BK_Atlas *atlas) {
  BK_ASSERT(atlas != nullptr);

  // Snapshot-and-clear before anything that can fail. A failure partway through must not
  // leave entries buffered to replay against a frame that never happened (spec 4.1).
  BK_AtlasEntry *pushed = atlas->pushed;
  i32 count = atlas->push_count;
  bool complete = true;
  atlas->push_count = 0;
  if (count == 0) {
    return complete;
  }

  atlas->in_flush = true;

  // Step 1: make every entry resident, dropping (texture_id = 0) any that fail. Keep
  // going past a failure -- every entry after the first must still be reported.
  for (i32 i = 0; i < count; i++) {
    u64 udata = pushed[i].udata;
    i32 index = s_make_resident(atlas, pushed[i].image_id, pushed[i].width, pushed[i].height);
    if (index < 0) {
      pushed[i].texture_id = 0;
      complete = false;
      continue;
    }
    s_fill_entry_from_record(&atlas->records[index], &pushed[i]);
    pushed[i].udata = udata;
  }

  // Step 2: group surviving entries by first-seen texture_id, in push order. At most
  // `count` distinct textures can appear in one flush, so that bounds this local list --
  // atlas counts are single digits in practice, but the bound has to hold regardless.
  u64 *group_textures =
      BK__ALLOC(&atlas->desc.allocator, BK_MEM_TAG_ATLAS, (isize)count * (isize)sizeof(u64));
  BK_AtlasSortItem *sort_items = BK__ALLOC(&atlas->desc.allocator, BK_MEM_TAG_ATLAS,
                                           (isize)count * (isize)sizeof(BK_AtlasSortItem));

  i32 group_count = 0;
  i32 sort_count = 0;
  for (i32 i = 0; i < count; i++) {
    if (pushed[i].texture_id == 0) {
      continue; // dropped in step 1
    }
    i32 group = -1;
    for (i32 g = 0; g < group_count; g++) {
      if (group_textures[g] == pushed[i].texture_id) {
        group = g;
        break;
      }
    }
    if (group < 0) {
      group = group_count;
      group_textures[group_count++] = pushed[i].texture_id;
    }
    sort_items[sort_count].group = group;
    sort_items[sort_count].index = i;
    sort_count++;
  }
  BK__FREE(&atlas->desc.allocator, BK_MEM_TAG_ATLAS, group_textures,
           (isize)count * (isize)sizeof(u64));

  // Step 3: sort by (group, push index).
  SDL_qsort(sort_items, (usize)sort_count, sizeof(BK_AtlasSortItem), s_compare_sort_items);

  // Step 4: emit one submit_batch call per run of equal group, via a scratch array kept
  // on the atlas so a frame with many entries does not realloc it on every flush.
  if (atlas->flush_batch_capacity < sort_count) {
    i32 wanted = atlas->flush_batch_capacity == 0 ? 16 : atlas->flush_batch_capacity * 2;
    while (wanted < sort_count) {
      wanted *= 2;
    }
    atlas->flush_batch =
        BK__REALLOC(&atlas->desc.allocator, BK_MEM_TAG_ATLAS, atlas->flush_batch,
                    (isize)atlas->flush_batch_capacity * (isize)sizeof(BK_AtlasEntry),
                    (isize)wanted * (isize)sizeof(BK_AtlasEntry));
    atlas->flush_batch_capacity = wanted;
  }

  i32 i = 0;
  while (i < sort_count) {
    i32 group = sort_items[i].group;
    i32 run_count = 0;
    while (i < sort_count && sort_items[i].group == group) {
      atlas->flush_batch[run_count++] = pushed[sort_items[i].index];
      i++;
    }
    atlas->desc.submit_batch(atlas->flush_batch, run_count, atlas->desc.udata);
  }

  BK__FREE(&atlas->desc.allocator, BK_MEM_TAG_ATLAS, sort_items,
           (isize)count * (isize)sizeof(BK_AtlasSortItem));
  atlas->in_flush = false;
  return complete;
}

/// A candidate for packing. Keyed by image_id rather than record index because
/// s_record_remove swap-removes, so any index held across a drop is stale.
typedef struct BK_AtlasCandidate {
  u64 image_id;
  i32 last_tick;
  i32 width, height;
} BK_AtlasCandidate;

/// Where the shelf packer decided a candidate goes. Separate from BK_AtlasCandidate so the
/// placement survives a candidate being dropped mid-pack by a get_pixels failure.
typedef struct BK_AtlasPlacement {
  u64 image_id;
  i32 min_x, min_y;
} BK_AtlasPlacement;

// Most-recently-seen first is what puts images drawn together onto one atlas; height
// descending is what makes shelf packing behave. As one total order they cooperate:
// everything pushed in a frame shares a last_tick, so within a frame this IS
// tallest-first, and older images queue behind (spec section 4.3).
static int s_compare_candidates(const void *lhs, const void *rhs) {
  const BK_AtlasCandidate *left = lhs;
  const BK_AtlasCandidate *right = rhs;
  if (left->last_tick != right->last_tick) {
    return left->last_tick > right->last_tick ? -1 : 1;
  }
  if (left->height != right->height) {
    return left->height > right->height ? -1 : 1;
  }
  // image_id last, so the order is total and the pack is reproducible run to run.
  return left->image_id < right->image_id ? -1 : 1;
}

/// Builds at most one atlas from pending lonely candidates. Returns 1 if it built an atlas,
/// 0 if it placed nothing -- below threshold, nothing fit, or every placed candidate's
/// get_pixels failed -- -1 on a failure that should make bk__atlas_defrag report false. Sets
/// *dropped to true (never clears it) if any candidate's get_pixels failed during this pack
/// -- the same "did less than asked" meaning bk__atlas_flush's return value already carries.
static i32 s_pack_one_atlas(BK_Atlas *atlas, bool *dropped) {
  // Step 1: collect candidates. Permanently-lonely records can never pack usefully and must
  // not gate the threshold either -- see spec section 4.3.
  i32 candidate_count = 0;
  for (i32 i = 0; i < atlas->record_count; i++) {
    const BK_AtlasRecord *record = &atlas->records[i];
    if (!record->permanent_lonely && !record->atlassed) {
      candidate_count++;
    }
  }
  if (candidate_count <= atlas->desc.defrag_lonely_threshold) {
    return 0;
  }

  BK_AtlasCandidate *candidates =
      BK__ALLOC(&atlas->desc.allocator, BK_MEM_TAG_ATLAS,
                (isize)candidate_count * (isize)sizeof(BK_AtlasCandidate));
  i32 filled = 0;
  for (i32 i = 0; i < atlas->record_count; i++) {
    const BK_AtlasRecord *record = &atlas->records[i];
    if (!record->permanent_lonely && !record->atlassed) {
      candidates[filled++] = (BK_AtlasCandidate){
          .image_id = record->image_id,
          .last_tick = record->last_tick,
          .width = record->width,
          .height = record->height,
      };
    }
  }

  // Step 2: sort.
  SDL_qsort(candidates, (usize)candidate_count, sizeof(BK_AtlasCandidate), s_compare_candidates);

  // Step 3: shelf-place.
  BK_AtlasPlacement *placements =
      BK__ALLOC(&atlas->desc.allocator, BK_MEM_TAG_ATLAS,
                (isize)candidate_count * (isize)sizeof(BK_AtlasPlacement));

  i32 size = atlas->desc.atlas_size;
  i32 shelf_x = 0, shelf_y = 0, shelf_height = 0;
  i32 placed_count = 0;
  i32 max_width = 0, max_height = 0;
  for (i32 i = 0; i < candidate_count; i++) {
    i32 width = candidates[i].width;
    i32 height = candidates[i].height;
    if (shelf_x + width > size) { // this row is full; start the next
      shelf_y += shelf_height;
      shelf_x = 0;
      shelf_height = 0;
    }
    if (shelf_y + height > size) {
      break; // the atlas is full; the rest stay pending for the next pack
    }
    // A candidate is never wider than size / 2, so one wrap always suffices.
    placements[placed_count++] =
        (BK_AtlasPlacement){.image_id = candidates[i].image_id, .min_x = shelf_x, .min_y = shelf_y};
    shelf_x += width;
    shelf_height = height > shelf_height ? height : shelf_height;
    max_width = width > max_width ? width : max_width;
    max_height = height > max_height ? height : max_height;
  }
  BK__FREE(&atlas->desc.allocator, BK_MEM_TAG_ATLAS, candidates,
           (isize)candidate_count * (isize)sizeof(BK_AtlasCandidate));
  if (placed_count == 0) {
    BK__FREE(&atlas->desc.allocator, BK_MEM_TAG_ATLAS, placements,
             (isize)candidate_count * (isize)sizeof(BK_AtlasPlacement));
    return 0;
  }

  // Step 4: allocate the atlas image, zero-filled so unpacked regions are not uninitialized.
  isize image_bytes = (isize)size * (isize)size * 4;
  u8 *image = BK__ALLOC_ZERO(&atlas->desc.allocator, BK_MEM_TAG_ATLAS, image_bytes);

  // Step 5: read pixels into scratch, sized to the largest placed image and reused, then
  // blit each one into the atlas image at its shelf position.
  isize scratch_bytes = (isize)max_width * (isize)max_height * 4;
  u8 *scratch = BK__ALLOC(&atlas->desc.allocator, BK_MEM_TAG_ATLAS, scratch_bytes);

  i32 surviving_count = 0;
  for (i32 i = 0; i < placed_count; i++) {
    u64 image_id = placements[i].image_id;
    i32 index = s_map_get(&atlas->map, image_id);
    BK_ASSERT(index >= 0);
    const BK_AtlasRecord *record = &atlas->records[index];
    i32 width = record->width;
    i32 height = record->height;
    // The callback's contract is size == this image's own byte count -- never the scratch
    // buffer's capacity, which is sized to the largest placed image and reused.
    usize bytes = (usize)width * (usize)height * 4u;
    // Guaranteed by s_make_resident, which rejects an image this large before a record
    // for it can exist. Asserted rather than re-checked so the invariant is visible here.
    BK_ASSERT(bytes <= (usize)INT32_MAX);
    if (!atlas->desc.get_pixels(image_id, scratch, (i32)bytes, width, height, atlas->desc.udata)) {
      s_log_image_failure(atlas, image_id, "get_pixels returned false");
      *dropped = true;
      if (record->texture_id != 0) {
        atlas->desc.destroy_texture(record->texture_id, atlas->desc.udata);
      }
      s_record_remove(atlas, index);
      // Mark dropped for step 7 to skip. image_id is not a legal sentinel here -- 0 is an
      // ordinary, valid image_id (spec section 4.3) -- but a real placement's min_x is
      // always >= 0, so -1 is unambiguous.
      placements[i].min_x = -1;
      continue;
    }
    i32 min_x = placements[i].min_x;
    i32 min_y = placements[i].min_y;
    for (i32 row = 0; row < height; row++) {
      usize dst_offset = ((usize)(min_y + row) * (usize)size + (usize)min_x) * 4u;
      SDL_memcpy(image + dst_offset, scratch + (usize)row * (usize)width * 4u, (usize)width * 4u);
    }
    surviving_count++;
  }
  BK__FREE(&atlas->desc.allocator, BK_MEM_TAG_ATLAS, scratch, scratch_bytes);

  // Step 6: a pack that placed nothing survives is a blank atlas no record points at.
  if (surviving_count == 0) {
    BK__FREE(&atlas->desc.allocator, BK_MEM_TAG_ATLAS, image, image_bytes);
    BK__FREE(&atlas->desc.allocator, BK_MEM_TAG_ATLAS, placements,
             (isize)candidate_count * (isize)sizeof(BK_AtlasPlacement));
    return 0;
  }

  u64 texture_id = atlas->desc.create_texture(image, size, size, atlas->desc.udata);
  BK__FREE(&atlas->desc.allocator, BK_MEM_TAG_ATLAS, image, image_bytes);
  if (texture_id == 0) {
    SDL_Log("BK: bk_atlas: defrag atlas texture creation failed (%d x %d)", size, size);
    BK__FREE(&atlas->desc.allocator, BK_MEM_TAG_ATLAS, placements,
             (isize)candidate_count * (isize)sizeof(BK_AtlasPlacement));
    // Do not destroy any lonely texture: every image is still drawable from where it was.
    return -1;
  }

  // Step 7: reserve the handle slot BEFORE any record adopts the atlas. Growing this list
  // is the last thing that can fail, and once step 8 has run there is no way back: the
  // lonely textures are destroyed, so an atlas that never made it into atlas_textures
  // would be invisible to both the dissolve pass and bk__atlas_destroy, and would leak
  // for the cache's lifetime. Reserving first leaves the atlas texture as the only thing
  // to undo.
  if (atlas->atlas_count == atlas->atlas_capacity) {
    i32 wanted = atlas->atlas_capacity == 0 ? 8 : atlas->atlas_capacity * 2;
    atlas->atlas_textures = BK__REALLOC(
        &atlas->desc.allocator, BK_MEM_TAG_ATLAS, atlas->atlas_textures,
        (isize)atlas->atlas_capacity * (isize)sizeof(u64), (isize)wanted * (isize)sizeof(u64));
    atlas->atlas_capacity = wanted;
  }

  // Step 8: only now, with the atlas handle in hand and its slot reserved, walk the
  // placements and adopt it.
  for (i32 i = 0; i < placed_count; i++) {
    if (placements[i].min_x < 0) {
      continue; // dropped in step 5
    }
    i32 index = s_map_get(&atlas->map, placements[i].image_id);
    BK_ASSERT(index >= 0);
    BK_AtlasRecord *record = &atlas->records[index];
    if (record->texture_id != 0) {
      atlas->desc.destroy_texture(record->texture_id, atlas->desc.udata);
    }
    record->texture_id = texture_id;
    record->atlassed = true;
    record->min_x = placements[i].min_x;
    record->min_y = placements[i].min_y;
  }
  BK__FREE(&atlas->desc.allocator, BK_MEM_TAG_ATLAS, placements,
           (isize)candidate_count * (isize)sizeof(BK_AtlasPlacement));

  // Step 9: append. Cannot fail -- step 7 reserved the slot.
  BK_ASSERT(atlas->atlas_count < atlas->atlas_capacity);
  atlas->atlas_textures[atlas->atlas_count++] = texture_id;

  return 1;
}

bool bk__atlas_defrag(BK_Atlas *atlas) {
  BK_ASSERT(atlas != nullptr);

  // 1. Dissolve atlases that are at least half decayed. Walk backwards: s_dissolve_atlas
  //    swap-removes from atlas_textures, so a forward walk would skip whichever handle it
  //    swaps into the slot just vacated. Re-reading atlas_textures[i] each iteration (rather
  //    than a copy taken up front) is what stays correct as the array shrinks under the walk.
  for (i32 i = atlas->atlas_count - 1; i >= 0; i--) {
    u64 texture_id = atlas->atlas_textures[i];
    i32 total = 0, decayed = 0;
    for (i32 index = 0; index < atlas->record_count; index++) {
      const BK_AtlasRecord *record = &atlas->records[index];
      if (record->atlassed && record->texture_id == texture_id) {
        total++;
        decayed += s_is_decayed(atlas, record) ? 1 : 0;
      }
    }
    // Stated positively on purpose. The donor computes total/decayed and fires when that
    // exceeds 0.5 -- true whenever anything has decayed at all, which is not what its own
    // comment describes. Port the intent (spec section 4.4).
    if (total > 0 && decayed * 2 >= total) {
      s_dissolve_atlas(atlas, texture_id, true);
    }
  }

  // 2. Destroy decayed lonely textures and forget their images.
  for (i32 i = atlas->record_count - 1; i >= 0; i--) {
    BK_AtlasRecord *record = &atlas->records[i];
    if (record->atlassed || !s_is_decayed(atlas, record)) {
      continue;
    }
    if (record->texture_id != 0) {
      atlas->desc.destroy_texture(record->texture_id, atlas->desc.udata);
    }
    s_record_remove(atlas, i);
  }

  // 3. Pack, as implemented in Task 4. `dropped` carries the same meaning `false` has in
  // bk__atlas_flush: an image the callbacks could not supply was dropped, so this call
  // did less than it was asked to. Only a hard failure stops the loop.
  bool ok = true;
  bool dropped = false;
  for (;;) {
    i32 packed = s_pack_one_atlas(atlas, &dropped);
    if (packed < 0) {
      ok = false;
      break;
    }
    if (packed == 0) {
      break; // under threshold, or nothing placed: stop rather than spin
    }
  }
  return ok && !dropped;
}
