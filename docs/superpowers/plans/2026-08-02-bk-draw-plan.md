# bk_draw (P3.3) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship `bk_draw.h` — a unified SDF draw layer where sprites and shapes share one batch, one shader pair, and one paint order — covering every call Space Delivery makes.

**Architecture:** Each `bk_draw_*` call links a `BK_DrawGeom` into an arena-allocated chain. Once per frame `bk__draw_collate()` stable-sorts by `(layer, record_id)`, packs the chain into contiguous `cmds`/`payload` arrays in two passes, uploads them as graphics storage buffers, and emits one `bk_gfx_draw_instanced(6, n)` per batch. One vertex shader expands each command's coverage quad from `gl_InstanceIndex`; one fragment shader evaluates every shape as an SDF.

**Tech Stack:** C23, SDL3 GPU, GLSL compiled offline by `glslc` + `spirv-cross`, CMake.

**Spec:** `docs/superpowers/specs/2026-08-02-bk-draw-design.md`. Read §2, §4, §5 before starting.

## Global Constraints

- **Language:** C23. Use `bool`/`true`/`false`, `nullptr`, designated initializers, compound literals, `constexpr`, `static_assert`, `[[nodiscard]]`. No VLAs (`-Wvla` is on), no `alloca`, no `#embed`.
- **Numerics:** use `bk_types.h`'s aliases (`i32`, `u32`, `f32`, `usize`), never `stdint.h`'s verbose names.
- **Naming:** public `bk_` + snake_case, public types `BK_` + PascalCase, enum values `BK_` + UPPER_SNAKE, internal linker-visible `bk__`, file-static `s_`.
- **Identifiers:** minimum 2 characters. `i`, `x`, `y`, `r`, `g`, `b`, `a` are the only exemptions (`.clang-tidy`).
- **Style:** run `cmake --build build --target format` before every commit. CI's `format` job blocks on `format-check`.
- **Includes, in order:** quoted/internal, then `<bielik/...>`, then `<SDL3/...>`, then system. `.clang-format`'s `IncludeBlocks: Regroup` enforces it.
- **Docs:** every public symbol gets a terse doc comment — one-sentence summary, param notes, lifetime notes where relevant.
- **Errors:** no silent failure. Failures log via `SDL_Log` with a `"BK: "` prefix. `BK_ASSERT` wraps `SDL_assert` and is live in Debug.
- **Build:** `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBK_WERROR=ON && cmake --build build`
- **Test:** `ctest --test-dir build --output-on-failure`
- **Worktree:** all work happens in `/Users/piotr/Work/GitHub/pusewicz/bielik2d-draw` on branch `draw`. All paths below are relative to it.
- **Commits:** imperative mood, human voice, no Conventional Commits prefixes, no AI signoffs. One commit per task.

## Shape type ids (used by every task from 3 onward)

These are `bk_draw`'s own numbering, not CF's. They appear in `BK_DrawGeom.type`, in `BK_DrawCmd.meta[0]`, and in `draw.vert`/`draw.frag`'s dispatch. Payload is indexed in `vec4` units.

| id | name | payload | notes |
|---|---|---|---|
| 0 | `TEXTURE` | `P0 = (dst.min.xy, dst.max.xy)`, `P1 = (uv.min.xy, uv.max.xy)` | no SDF, no AA |
| 1 | `BOX` | `P0 = (center.xy, half_extents.xy)` | `shape[0]` = corner radius |
| 2 | `CIRCLE` | `P0 = (center.xy, 0, 0)` | `shape[0]` = radius |
| 3 | `SEGMENT` | `P0 = (a.xy, b.xy)` | capsule; `shape[1]` = half-thickness |
| 4 | `TRI` | `P0 = (p0.xy, p1.xy)`, `P1 = (p2.xy, 0, 0)` | `shape[0]` = corner radius |
| 5 | `ARROW` | `P0 = (a.xy, b.xy)`, `P1 = (shaft_radius, head_width, 0, 0)` | |

Shapes carry **no rotation basis**. Rotation lives in the per-command matrix, so the SDF always evaluates in the axis-aligned local space the caller used. This is a deliberate simplification over CF's `distance_box`, which takes a `u` basis vector because CF bakes some transforms CPU-side.

## File Structure

| File | Responsibility |
|---|---|
| `include/bielik/bk_draw.h` | The entire public API. Nothing else is public. |
| `src/internal/bk_draw_internal.h` | `BK_DrawGeom`, `BK_DrawCmd`, `BK_DrawBatch`, `BK_DrawPacked`; `bk__draw_init`/`_pack`/`_collate`/`_shutdown`; the test-visible record accessors. |
| `src/bk_draw.c` | Record layer, state stacks, sort, packer, GPU resource ownership, collate. One file: the record and pack halves share the stack state and the type table, and splitting them would mean exporting internals purely to re-import them. |
| `shaders/draw.vert` | Coverage-quad expansion from `gl_InstanceIndex`. |
| `shaders/draw.frag` | SDF evaluation and compositing. |
| `cmake/shaders.cmake` | Gains `bk_embed_shader`, which turns committed bytecode into a C array header. |
| `src/internal/bk_gfx_texture_internal.h`, `src/bk_gfx_texture.c` | Gain `bk__gfx_texture_size`. |
| `src/bk_app.c` | Three added calls: init, collate, shutdown. |
| `tests/test_draw.c` | CPU-only, CI-required on every platform. |
| `tests/test_draw_gpu.c` | Needs a device; joins CI's allow-failure GPU group. |
| `samples/08_draw/` | Living documentation for the whole API. |

---

### Task 1: The public header

**Files:**
- Create: `include/bielik/bk_draw.h`
- Create: `tests/test_header_bk_draw.c`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `BK_Aabb`, `BK_Color`, `BK_M3x2`, `BK_Rect`, `BK_V2` from `bielik/bk_math.h`; `i32`/`f32` from `bielik/bk_types.h`.
- Produces: the complete public API every later task implements. `BK_DRAW_STACK_MAX`. No implementation yet — this task only proves the header parses standalone.

- [ ] **Step 1: Write the failing test**

Create `tests/test_header_bk_draw.c` — this mirrors the existing `test_header_bk_math.c` pattern exactly (a one-line TU proving the header pulls in its own dependencies):

```c
#include <bielik/bk_draw.h>
```

- [ ] **Step 2: Register it and run to verify it fails**

Add to `tests/CMakeLists.txt`, next to the other `test_header_*` entries:

```cmake
add_executable(test_header_bk_draw test_header_bk_draw.c)
target_link_libraries(test_header_bk_draw PRIVATE bielik bk_warnings)
add_test(NAME test_header_bk_draw COMMAND test_header_bk_draw)
```

Run: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBK_WERROR=ON && cmake --build build --target test_header_bk_draw`
Expected: FAIL — `fatal error: 'bielik/bk_draw.h' file not found`.

Note: a header-only TU has no `main`, which is why the existing `test_header_*` files are compiled but their `add_test` only runs a trivial binary. Follow whatever the existing entries do verbatim — check `tests/CMakeLists.txt` for how `test_header_bk_math` handles it and copy that shape.

- [ ] **Step 3: Write the header**

Create `include/bielik/bk_draw.h` with exactly the contents of spec §2. Copy it verbatim from `docs/superpowers/specs/2026-08-02-bk-draw-design.md` — the spec's code block is the normative source, including every doc comment.

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build build --target test_header_bk_draw && ctest --test-dir build -R test_header_bk_draw --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Format and commit**

```bash
cmake --build build --target format
git add include/bielik/bk_draw.h tests/test_header_bk_draw.c tests/CMakeLists.txt
git commit -m "add the bk_draw public header"
```

---

### Task 2: State stacks and the record chain

**Files:**
- Create: `src/bk_draw.c`
- Create: `src/internal/bk_draw_internal.h`
- Create: `tests/test_draw.c`
- Modify: `CMakeLists.txt:33` (add `src/bk_draw.c` to the `bielik` target)
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `bk_frame_alloc(usize size, usize align)` from `bielik/bk_app.h`; `bk_m3x2_identity()`, `bk_m3x2_mul()`, `bk_m3x2_translation()`, `bk_m3x2_rotation()`, `bk_m3x2_scale()`, `bk_color_white()`, `bk_color_premultiply()` from `bielik/bk_math.h`.
- Produces: `BK_DrawGeom` and the accessors `bk__draw_get_geom_count()` / `bk__draw_get_geom(i32 index)` / `bk__draw_reset()`, all used by Task 3's packer and by every CPU test.

- [ ] **Step 1: Write the failing test**

Create `tests/test_draw.c`. This follows `tests/test_gfx_drawlist.c`'s model: no GPU, no window, no booted app — `bk_frame_alloc` creates its arena lazily, so records can be built and inspected on any platform.

```c
#include "bk_test.h"
#include "internal/bk_draw_internal.h"

#include <bielik/bk_draw.h>
#include <bielik/bk_math.h>

#include <stddef.h> // offsetof, used by Task 3's layout test
#include <stdio.h>

static void test_color_stack_pushes_pops_and_peeks(void) {
  bk__draw_reset();
  REQUIRE(bk_draw_peek_color().r == 1.0f); // default is white

  bk_draw_push_color((BK_Color){0.25f, 0.5f, 0.75f, 1.0f});
  REQUIRE(bk_draw_peek_color().r == 0.25f);

  bk_draw_push_color((BK_Color){0.0f, 0.0f, 0.0f, 1.0f});
  REQUIRE(bk_draw_peek_color().r == 0.0f);

  BK_Color popped = bk_draw_pop_color();
  REQUIRE(popped.r == 0.0f);            // pop returns what it removed
  REQUIRE(bk_draw_peek_color().r == 0.25f); // and restores the one beneath

  bk_draw_pop_color();
  REQUIRE(bk_draw_peek_color().r == 1.0f);
}

static void test_records_snapshot_their_own_state(void) {
  bk__draw_reset();

  bk_draw_push_color((BK_Color){1.0f, 0.0f, 0.0f, 1.0f});
  bk_draw_push_layer(3);
  bk_draw_box_fill(bk_aabb(bk_v2(-1.0f, -1.0f), bk_v2(1.0f, 1.0f)), 0.0f);
  bk_draw_pop_layer();
  bk_draw_pop_color();

  bk_draw_box_fill(bk_aabb(bk_v2(-1.0f, -1.0f), bk_v2(1.0f, 1.0f)), 0.0f);

  REQUIRE(bk__draw_get_geom_count() == 2);

  // Each record kept the state bound when *it* was recorded. If records referenced
  // shared state, both would report the post-pop defaults.
  const BK_DrawGeom *first = bk__draw_get_geom(0);
  const BK_DrawGeom *second = bk__draw_get_geom(1);
  REQUIRE(first != nullptr && second != nullptr);
  REQUIRE(first->layer == 3);
  REQUIRE(second->layer == 0);
  REQUIRE(first->color.g == 0.0f); // red, premultiplied
  REQUIRE(second->color.g == 1.0f); // white
  // Order matches call order, not reverse.
  REQUIRE(first->record_id == 0);
  REQUIRE(second->record_id == 1);
}

static void test_transform_composes_and_restores(void) {
  bk__draw_reset();
  REQUIRE(bk_draw_peek().origin.x == 0.0f);

  bk_draw_push();
  bk_draw_translate(bk_v2(10.0f, 0.0f));
  REQUIRE_NEAR(bk_draw_peek().origin.x, 10.0f, 1e-5);

  bk_draw_push();
  bk_draw_translate(bk_v2(5.0f, 0.0f));
  REQUIRE_NEAR(bk_draw_peek().origin.x, 15.0f, 1e-5); // composes, not replaces

  bk_draw_pop();
  REQUIRE_NEAR(bk_draw_peek().origin.x, 10.0f, 1e-5);
  bk_draw_pop();
  REQUIRE_NEAR(bk_draw_peek().origin.x, 0.0f, 1e-5);
}

static void test_records_capture_the_camera_transform(void) {
  bk__draw_reset();

  bk_draw_push();
  bk_draw_translate(bk_v2(100.0f, 0.0f));
  bk_draw_circle_fill(bk_v2(0.0f, 0.0f), 4.0f);
  bk_draw_pop();

  const BK_DrawGeom *geom = bk__draw_get_geom(0);
  REQUIRE(geom != nullptr);
  REQUIRE_NEAR(geom->transform.origin.x, 100.0f, 1e-5);
}

static void test_singular_transform_is_culled(void) {
  bk__draw_reset();

  bk_draw_push();
  bk_draw_scale(bk_v2(0.0f, 1.0f)); // collapses x -- determinant is zero
  bk_draw_box_fill(bk_aabb(bk_v2(-1.0f, -1.0f), bk_v2(1.0f, 1.0f)), 0.0f);
  bk_draw_pop();

  // Dropped at record time: no log, no assert. A sprite scaled to zero is legitimate
  // game state that correctly draws nothing.
  REQUIRE(bk__draw_get_geom_count() == 0);

  bk_draw_box_fill(bk_aabb(bk_v2(-1.0f, -1.0f), bk_v2(1.0f, 1.0f)), 0.0f);
  REQUIRE(bk__draw_get_geom_count() == 1);
}

static void test_reset_clears_records_and_stacks(void) {
  bk__draw_reset();
  bk_draw_push_color((BK_Color){0.0f, 0.0f, 0.0f, 1.0f});
  bk_draw_push_layer(9);
  bk_draw_box_fill(bk_aabb(bk_v2(-1.0f, -1.0f), bk_v2(1.0f, 1.0f)), 0.0f);

  bk__draw_reset();

  REQUIRE(bk__draw_get_geom_count() == 0);
  REQUIRE(bk__draw_get_geom(0) == nullptr);
  // A push/pop imbalance must not leak into the next frame.
  REQUIRE(bk_draw_peek_color().r == 1.0f);
  REQUIRE(bk_draw_peek_layer() == 0);
}

int main(void) {
  test_color_stack_pushes_pops_and_peeks();
  test_records_snapshot_their_own_state();
  test_transform_composes_and_restores();
  test_records_capture_the_camera_transform();
  test_singular_transform_is_culled();
  test_reset_clears_records_and_stacks();
  printf("test_draw: OK\n");
  return 0;
}
```

- [ ] **Step 2: Register it and run to verify it fails**

Add to `tests/CMakeLists.txt`, following `test_gfx_drawlist`'s shape (note `target_include_directories` — the test includes an internal header):

```cmake
# Record-structure tests: no GPU, so this one is a required CI check everywhere.
add_executable(test_draw test_draw.c)
target_link_libraries(test_draw PRIVATE bielik bk_warnings)
target_include_directories(test_draw PRIVATE ${PROJECT_SOURCE_DIR}/src)
add_test(NAME test_draw COMMAND test_draw)
```

Run: `cmake --build build --target test_draw`
Expected: FAIL — `'internal/bk_draw_internal.h' file not found`.

- [ ] **Step 3: Write the internal header**

Create `src/internal/bk_draw_internal.h`:

```c
#pragma once
#include <bielik/bk_draw.h>
#include <bielik/bk_math.h>
#include <bielik/bk_types.h>

/// Shape type ids, shared with draw.vert / draw.frag's dispatch. The numbering is
/// bk_draw's own, not Cute Framework's.
typedef enum BK_DrawType {
  BK_DRAW_TYPE_TEXTURE,
  BK_DRAW_TYPE_BOX,
  BK_DRAW_TYPE_CIRCLE,
  BK_DRAW_TYPE_SEGMENT,
  BK_DRAW_TYPE_TRI,
  BK_DRAW_TYPE_ARROW,
} BK_DrawType;

/// One recorded drawable, linked into the frame's arena-allocated chain. Every field is
/// a snapshot taken when the bk_draw_* call ran -- nothing here points at shared state,
/// so later stack changes cannot retroactively alter an earlier record.
typedef struct BK_DrawGeom {
  struct BK_DrawGeom *next;
  i32 record_id; // increments per record; the tiebreaker in the (layer, record_id) sort
  BK_DrawType type;
  BK_Color color; // premultiplied
  // Type-dependent, four slots because TEXTURE needs the most:
  //   TEXTURE  [0]=dst.min [1]=dst.max [2]=src_px.min [3]=src_px.max
  //   BOX      [0]=centre  [1]=half_extents
  //   CIRCLE   [0]=centre
  //   SEGMENT  [0]=a       [1]=b
  //   TRI      [0]=p0      [1]=p1  [2]=p2
  //   ARROW    [0]=a       [1]=b   [2]=(shaft_radius, head_width)
  BK_V2 shape[4];
  f32 radius; // corner/circle radius
  f32 half_stroke;  // 0 => filled
  f32 aa_px;        // antialias band, in pixels
  bool fill;
  i32 layer;
  BK_Rect scissor;
  BK_GfxTexture *texture; // TEXTURE only; nullptr otherwise
  BK_M3x2 transform;      // the camera transform at record time (projection NOT applied)
} BK_DrawGeom;

/// Clears the record chain and resets every state stack to its default. Called at the
/// end of bk__draw_collate, and directly by tests to isolate cases.
void bk__draw_reset(void);

/// Number of records in the current frame's chain. Framework-internal; tests only.
i32 bk__draw_get_geom_count(void);

/// The record at index in record order, or nullptr if out of range. Framework-internal;
/// tests only -- walks the chain, so it is O(index).
const BK_DrawGeom *bk__draw_get_geom(i32 index);
```

- [ ] **Step 4: Write the record layer**

Create `src/bk_draw.c`. Implement, in this order:

1. **File-static state.** One struct holding: chain `head`/`tail`, `record_count`; and for each stack, a fixed `BK_DRAW_STACK_MAX` array plus a `count`. Stacks store their base value at index 0 and start with `count == 1`.

```c
typedef struct BK_DrawState {
  BK_DrawGeom *head;
  BK_DrawGeom *tail;
  i32 record_count;

  BK_M3x2 transforms[BK_DRAW_STACK_MAX];
  i32 transform_count;
  BK_Color colors[BK_DRAW_STACK_MAX];
  i32 color_count;
  i32 layers[BK_DRAW_STACK_MAX];
  i32 layer_count;
  f32 antialias[BK_DRAW_STACK_MAX];
  i32 antialias_count;
  BK_Rect scissors[BK_DRAW_STACK_MAX];
  i32 scissor_count;

  BK_M3x2 projection;
  bool has_projection;
} BK_DrawState;

static BK_DrawState s_draw;
static bool s_initialized_stacks;
```

2. **`s_ensure_stacks()`** — lazily seeds every stack's base entry the first time anything touches `s_draw`, so tests need no explicit init. Base values: transform `bk_m3x2_identity()`, color `bk_color_white()`, layer `0`, antialias `1.0f`, scissor `(BK_Rect){0}`.

3. **The stack triplets.** Write one push/pop/peek trio per stack. They are mechanically identical; here is the color one in full, and the other four follow the same shape with their own array and type:

```c
void bk_draw_push_color(BK_Color color) {
  s_ensure_stacks();
  BK_ASSERT(s_draw.color_count < BK_DRAW_STACK_MAX);
  if (s_draw.color_count >= BK_DRAW_STACK_MAX) {
    return;
  }
  s_draw.colors[s_draw.color_count++] = color;
}

BK_Color bk_draw_pop_color(void) {
  s_ensure_stacks();
  BK_ASSERT(s_draw.color_count > 1);
  if (s_draw.color_count <= 1) {
    return s_draw.colors[0];
  }
  return s_draw.colors[--s_draw.color_count];
}

BK_Color bk_draw_peek_color(void) {
  s_ensure_stacks();
  return s_draw.colors[s_draw.color_count - 1];
}
```

4. **The camera stack.** `bk_draw_push` duplicates the top (so `translate` after `push` composes onto a copy); `bk_draw_pop` drops it; `bk_draw_peek` returns the top. The four transform calls compose onto the top:

```c
void bk_draw_transform(BK_M3x2 transform) {
  s_ensure_stacks();
  BK_M3x2 *top = &s_draw.transforms[s_draw.transform_count - 1];
  *top = bk_m3x2_mul(*top, transform);
}

void bk_draw_translate(BK_V2 offset) { bk_draw_transform(bk_m3x2_translation(offset)); }
void bk_draw_rotate(f32 radians) { bk_draw_transform(bk_m3x2_rotation(radians)); }
void bk_draw_scale(BK_V2 scale) { bk_draw_transform(bk_m3x2_scale(scale)); }
```

Check `bk_m3x2_mul`'s argument order in `include/bielik/bk_math.h` before writing this — the composition must be such that `push; translate(10,0); translate(5,0)` leaves `origin.x == 15`, which the test asserts. Swap the operands if the first attempt gives 5.

5. **`s_record()`** — the one place a `BK_DrawGeom` is built and linked:

```c
static BK_DrawGeom *s_record(BK_DrawType type) {
  s_ensure_stacks();
  BK_M3x2 transform = s_draw.transforms[s_draw.transform_count - 1];

  // Singular-transform cull: a shape scaled to zero rasterizes to zero area. Dropping
  // it here skips packing and uploading a record that draws nothing. Not an error --
  // no log, no assert.
  f32 det = transform.x.x * transform.y.y - transform.y.x * transform.x.y;
  if (det == 0.0f) {
    return nullptr;
  }

  BK_DrawGeom *geom = bk_frame_alloc(sizeof *geom, alignof(BK_DrawGeom));
  if (geom == nullptr) {
    return nullptr;
  }
  *geom = (BK_DrawGeom){
      .record_id = s_draw.record_count++,
      .type = type,
      .color = bk_color_premultiply(s_draw.colors[s_draw.color_count - 1]),
      .aa_px = s_draw.antialias[s_draw.antialias_count - 1],
      .layer = s_draw.layers[s_draw.layer_count - 1],
      .scissor = s_draw.scissors[s_draw.scissor_count - 1],
      .transform = transform,
  };
  if (s_draw.tail == nullptr) {
    s_draw.head = geom;
  } else {
    s_draw.tail->next = geom;
  }
  s_draw.tail = geom;
  return geom;
}
```

6. **The nine shape entry points.** Each calls `s_record`, fills `shape[]`/`radius`/`half_stroke`/`fill` per the payload table, and returns. `bk_draw_texture` additionally stores `texture`. Stroked variants set `half_stroke = thickness * 0.5f` and `fill = false`; filled variants set `half_stroke = 0.0f` and `fill = true`. Two examples; write the other seven the same way:

```c
void bk_draw_box_fill(BK_Aabb bb, f32 radius) {
  BK_DrawGeom *geom = s_record(BK_DRAW_TYPE_BOX);
  if (geom == nullptr) {
    return;
  }
  geom->shape[0] = bk_aabb_center(bb);
  geom->shape[1] = bk_aabb_half_extents(bb);
  geom->radius = radius;
  geom->fill = true;
}

void bk_draw_line(BK_V2 p0, BK_V2 p1, f32 thickness) {
  BK_DrawGeom *geom = s_record(BK_DRAW_TYPE_SEGMENT);
  if (geom == nullptr) {
    return;
  }
  geom->shape[0] = p0;
  geom->shape[1] = p1;
  geom->half_stroke = thickness * 0.5f;
  geom->fill = true; // a line is a filled capsule, not a stroked outline
}
```

7. **`bk__draw_reset`, `bk__draw_get_geom_count`, `bk__draw_get_geom`.** Reset zeroes the chain pointers and `record_count`, then re-seeds the stacks. It must **not** free anything — the records live in the frame arena.

8. **`bk_draw_set_projection`** stores into `s_draw.projection` and sets `has_projection`. Nothing reads it until Task 5.

- [ ] **Step 5: Add the source file to the build and run the test**

In `CMakeLists.txt:33`, add `src/bk_draw.c` to the `add_library(bielik STATIC ...)` source list, keeping the list alphabetical (`src/bk_app.c src/bk_draw.c src/bk_gfx.c ...`).

Run: `cmake --build build --target test_draw && ctest --test-dir build -R "^test_draw$" --output-on-failure`
Expected: PASS, printing `test_draw: OK`.

- [ ] **Step 6: Format and commit**

```bash
cmake --build build --target format
git add src/bk_draw.c src/internal/bk_draw_internal.h tests/test_draw.c tests/CMakeLists.txt CMakeLists.txt
git commit -m "record bk_draw shapes into a per-frame chain"
```

---

### Task 3: Sort, pack, and batch

**Files:**
- Modify: `src/internal/bk_draw_internal.h`
- Modify: `src/bk_draw.c`
- Modify: `tests/test_draw.c`

**Interfaces:**
- Consumes: `BK_DrawGeom`, `bk__draw_reset`, `bk__draw_get_geom` from Task 2.
- Produces: `BK_DrawCmd` (48 bytes, three `vec4`s), `BK_DrawBatch`, `BK_DrawPacked`, and `bool bk__draw_pack(BK_DrawPacked *out, i32 target_w, i32 target_h)`. Task 5 calls `bk__draw_pack` and uploads its output; no other task reads these types.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_draw.c`, and add the new cases to `main`:

```c
static void test_packed_cmd_matches_the_shader_layout(void) {
  // Three vec4s, std430. draw.vert's `Cmd` struct must match this byte-for-byte.
  REQUIRE(sizeof(BK_DrawCmd) == 48);
  REQUIRE(offsetof(BK_DrawCmd, meta) == 0);
  REQUIRE(offsetof(BK_DrawCmd, shape) == 16);
  REQUIRE(offsetof(BK_DrawCmd, misc) == 32);
}

static void test_layers_sort_above_record_order(void) {
  bk__draw_reset();

  bk_draw_push_layer(5);
  bk_draw_box_fill(bk_aabb(bk_v2(-1.0f, -1.0f), bk_v2(1.0f, 1.0f)), 0.0f); // record 0
  bk_draw_pop_layer();
  bk_draw_circle_fill(bk_v2(0.0f, 0.0f), 1.0f);                            // record 1, layer 0

  BK_DrawPacked packed = {0};
  REQUIRE(bk__draw_pack(&packed, 640, 480));
  REQUIRE(packed.cmd_count == 2);
  // Layer 0 paints first even though it was recorded second.
  REQUIRE(packed.cmds[0].meta[0] == BK_DRAW_TYPE_CIRCLE);
  REQUIRE(packed.cmds[1].meta[0] == BK_DRAW_TYPE_BOX);
}

static void test_equal_layers_keep_record_order(void) {
  bk__draw_reset();

  bk_draw_circle_fill(bk_v2(0.0f, 0.0f), 1.0f);                            // record 0
  bk_draw_box_fill(bk_aabb(bk_v2(-1.0f, -1.0f), bk_v2(1.0f, 1.0f)), 0.0f); // record 1
  bk_draw_circle_fill(bk_v2(0.0f, 0.0f), 2.0f);                            // record 2

  BK_DrawPacked packed = {0};
  REQUIRE(bk__draw_pack(&packed, 640, 480));
  REQUIRE(packed.cmd_count == 3);
  // The sort is stable, so equal layers preserve the order they were recorded in.
  REQUIRE(packed.cmds[0].meta[0] == BK_DRAW_TYPE_CIRCLE);
  REQUIRE(packed.cmds[1].meta[0] == BK_DRAW_TYPE_BOX);
  REQUIRE(packed.cmds[2].meta[0] == BK_DRAW_TYPE_CIRCLE);
}

static void test_batches_split_on_texture_and_scissor_only(void) {
  static int dummy_a, dummy_b;
  BK_GfxTexture *texture_a = (BK_GfxTexture *)&dummy_a;
  BK_GfxTexture *texture_b = (BK_GfxTexture *)&dummy_b;
  BK_Aabb unit = bk_aabb(bk_v2(-1.0f, -1.0f), bk_v2(1.0f, 1.0f));

  bk__draw_reset();
  // Color, layer, antialias and shape type all change -- none of them may split.
  bk_draw_box_fill(unit, 0.0f);
  bk_draw_push_color((BK_Color){1.0f, 0.0f, 0.0f, 1.0f});
  bk_draw_circle_fill(bk_v2(0.0f, 0.0f), 1.0f);
  bk_draw_pop_color();
  bk_draw_push_antialias(0.0f);
  bk_draw_line(bk_v2(-1.0f, 0.0f), bk_v2(1.0f, 0.0f), 1.0f);
  bk_draw_pop_antialias();

  BK_DrawPacked packed = {0};
  REQUIRE(bk__draw_pack(&packed, 640, 480));
  REQUIRE(packed.batch_count == 1);
  REQUIRE(packed.batches[0].first == 0);
  REQUIRE(packed.batches[0].count == 3);

  bk__draw_reset();
  bk_draw_texture(texture_a, unit, unit);
  bk_draw_texture(texture_a, unit, unit); // same texture -- no split
  bk_draw_texture(texture_b, unit, unit); // different texture -- splits
  REQUIRE(bk__draw_pack(&packed, 640, 480));
  REQUIRE(packed.batch_count == 2);
  REQUIRE(packed.batches[0].count == 2);
  REQUIRE(packed.batches[1].count == 1);
  REQUIRE(packed.batches[0].texture == texture_a);
  REQUIRE(packed.batches[1].texture == texture_b);

  bk__draw_reset();
  bk_draw_box_fill(unit, 0.0f);
  bk_draw_push_scissor((BK_Rect){.x = 0, .y = 0, .width = 10, .height = 10});
  bk_draw_box_fill(unit, 0.0f); // different scissor -- splits
  bk_draw_pop_scissor();
  REQUIRE(bk__draw_pack(&packed, 640, 480));
  REQUIRE(packed.batch_count == 2);
}

static void test_matrix_palette_dedupes_identical_transforms(void) {
  BK_Aabb unit = bk_aabb(bk_v2(-1.0f, -1.0f), bk_v2(1.0f, 1.0f));

  bk__draw_reset();
  bk_draw_box_fill(unit, 0.0f);
  bk_draw_box_fill(unit, 0.0f);
  bk_draw_box_fill(unit, 0.0f);

  BK_DrawPacked packed = {0};
  REQUIRE(bk__draw_pack(&packed, 640, 480));
  // A frame that never touches the camera emits exactly one palette entry, and every
  // command selects it.
  REQUIRE(packed.cmds[0].meta[3] == packed.cmds[1].meta[3]);
  REQUIRE(packed.cmds[1].meta[3] == packed.cmds[2].meta[3]);

  bk__draw_reset();
  bk_draw_box_fill(unit, 0.0f);
  bk_draw_push();
  bk_draw_translate(bk_v2(50.0f, 0.0f));
  bk_draw_box_fill(unit, 0.0f);
  bk_draw_pop();
  REQUIRE(bk__draw_pack(&packed, 640, 480));
  REQUIRE(packed.cmds[0].meta[3] != packed.cmds[1].meta[3]);
}

static void test_payload_offsets_match_each_type(void) {
  BK_Aabb unit = bk_aabb(bk_v2(-2.0f, -3.0f), bk_v2(4.0f, 5.0f));

  bk__draw_reset();
  bk_draw_box_fill(unit, 0.0f);                        // 1 payload vec4
  bk_draw_tri_fill(bk_v2(0, 0), bk_v2(1, 0), bk_v2(0, 1), 0.0f); // 2 payload vec4s

  BK_DrawPacked packed = {0};
  REQUIRE(bk__draw_pack(&packed, 640, 480));

  // The box's payload holds centre then half-extents.
  u32 box_offset = packed.cmds[0].meta[2];
  REQUIRE_NEAR(packed.payload[box_offset].x, 1.0f, 1e-5);  // centre x = (-2+4)/2
  REQUIRE_NEAR(packed.payload[box_offset].y, 1.0f, 1e-5);  // centre y = (-3+5)/2
  REQUIRE_NEAR(packed.payload[box_offset].z, 3.0f, 1e-5);  // half-extent x
  REQUIRE_NEAR(packed.payload[box_offset].w, 4.0f, 1e-5);  // half-extent y

  // The triangle's payload starts after the box's single vec4.
  u32 tri_offset = packed.cmds[1].meta[2];
  REQUIRE(tri_offset == box_offset + 1);
  REQUIRE(packed.payload_count >= tri_offset + 2);
}
```

- [ ] **Step 2: Run to verify they fail**

Run: `cmake --build build --target test_draw`
Expected: FAIL — `unknown type name 'BK_DrawCmd'`.

- [ ] **Step 3: Extend the internal header**

Append to `src/internal/bk_draw_internal.h`:

```c
/// One GPU command, mirroring `Cmd` in draw.vert (std430, three vec4s). Colors travel
/// as packed half4 -- two packHalf2x16 words, rg in meta[1] and ba in misc[1] -- rather
/// than unorm8, so premultiplied channels above 1.0 survive the trip.
typedef struct BK_DrawCmd {
  u32 meta[4];  // type, color_rg, payload offset (vec4 units), matrix palette offset
  f32 shape[4]; // radius, half-stroke, antialias band (world units), unused
  f32 misc[4];  // fill (0 or 1), color_ba reinterpreted as f32, 2 unused
} BK_DrawCmd;

static_assert(sizeof(BK_DrawCmd) == 48, "std430 layout: three vec4s");

/// A payload slot. Named rather than using f32[4] so the packer's intent reads clearly.
typedef struct BK_DrawV4 {
  f32 x, y, z, w;
} BK_DrawV4;

/// A contiguous run of commands sharing a texture and a scissor -- one instanced draw.
typedef struct BK_DrawBatch {
  i32 first;
  i32 count;
  BK_GfxTexture *texture; // nullptr for shape-only batches
  BK_Rect scissor;
} BK_DrawBatch;

/// The packer's output. Every array is a single frame-arena allocation, valid until the
/// next bk__arena_reset.
typedef struct BK_DrawPacked {
  BK_DrawCmd *cmds;
  i32 cmd_count;
  BK_DrawV4 *payload;
  i32 payload_count;
  BK_DrawBatch *batches;
  i32 batch_count;
} BK_DrawPacked;

/// Snapshots and clears the record chain, stable-sorts it by (layer, record_id), and
/// packs it into contiguous arrays sized exactly. target_w/target_h are the render
/// target's pixel dimensions, used to convert the antialias band from pixels into the
/// world units the shader evaluates in. Returns false if there was nothing to draw or
/// an arena allocation failed; *out is zeroed in that case. Does NOT touch the GPU, so
/// tests can call it with no device.
bool bk__draw_pack(BK_DrawPacked *out, i32 target_w, i32 target_h);
```

- [ ] **Step 4: Implement the packer**

In `src/bk_draw.c`, add:

1. **`s_half4_pack(f32 lo, f32 hi)`** — packs two floats into one `u32` as two IEEE half-precision values, matching GLSL's `packHalf2x16`. Write the conversion by hand (sign, exponent bias 15, 10 mantissa bits, flush denormals to zero); there is no C23 standard `_Float16` guarantee across the toolchains here.

2. **`s_payload_size(BK_DrawType type)`** — returns 1 for `BOX`/`CIRCLE`/`SEGMENT`, 2 for `TEXTURE`/`TRI`/`ARROW`. Used by both passes, so the two can never disagree.

3. **`bk__draw_pack`**:

```c
bool bk__draw_pack(BK_DrawPacked *out, i32 target_w, i32 target_h) {
  *out = (BK_DrawPacked){0};

  // Snapshot and clear FIRST, before anything can fail or return early. bk_app.c runs
  // bk__gfx_flush() then bk__arena_reset() unconditionally, and flush has early returns
  // (failed command-buffer acquire, null swapchain texture on a minimised window). A
  // chain left pointing into arena memory across a reset is a use-after-free the moment
  // the arena has grown via bk__realloc.
  BK_DrawGeom *head = s_draw.head;
  s_draw.head = nullptr;
  s_draw.tail = nullptr;
  if (head == nullptr) {
    return false;
  }
  ...
}
```

   Then, in order:

   - **Pass one — count.** Walk the chain accumulating `cmd_count` and `payload_count` (via `s_payload_size`). Reserve 2 extra `vec4`s per *distinct* transform; since dedup only compares against the immediately preceding record, an upper bound of `2 * cmd_count` is exact enough and costs nothing to over-reserve within one arena allocation. Allocate `cmds`, `payload`, and `batches` (at most `cmd_count` batches) with `bk_frame_alloc`.
   - **Sort.** Build an array of `BK_DrawGeom *` in chain order, then stable-sort by `layer`. Use an insertion sort — it is stable by construction, needs no comparator plumbing, and typical frames are nearly sorted already. Do **not** use `qsort`: it is not required to be stable, which would break `test_equal_layers_keep_record_order`.
   - **Pass two — fill.** For each sorted record: emit its palette entry if its `transform` differs from the previous record's (compare all six floats), writing `projection × transform` as two `vec4`s; write its payload `vec4`s per the type table; write its `BK_DrawCmd`. Open a new batch whenever `texture` or `scissor` differs from the previous record's.
   - **The antialias band.** `shape[2]` is the band in **world** units, not pixels, because the fragment shader evaluates the SDF in world space. Compute it per palette entry from the composed matrix `mvp` and the target size:

```c
// mvp maps world units to NDC ([-1,1]), so a world unit spans |mvp.x| * target_w/2
// pixels horizontally. Take the larger axis so the band never under-covers.
f32 scale_x = bk_v2_len(mvp.x) * (f32)target_w * 0.5f;
f32 scale_y = bk_v2_len(mvp.y) * (f32)target_h * 0.5f;
f32 scale = bk_maxf(scale_x, scale_y);
f32 aa_world = scale > 0.0f ? geom->aa_px / scale : 0.0f;
```

   - **The projection.** Use `s_draw.projection` when `has_projection` is set, otherwise `bk_m3x2_ortho((f32)target_w, (f32)target_h)`.

- [ ] **Step 5: Run to verify they pass**

Run: `cmake --build build --target test_draw && ctest --test-dir build -R "^test_draw$" --output-on-failure`
Expected: PASS.

- [ ] **Step 6: Format and commit**

```bash
cmake --build build --target format
git add src/bk_draw.c src/internal/bk_draw_internal.h tests/test_draw.c
git commit -m "sort, pack, and batch the bk_draw record chain"
```

---

### Task 4: The shaders and their build-time embedding

**Files:**
- Create: `shaders/draw.vert`, `shaders/draw.frag`
- Create: `cmake/embed_shader.cmake`
- Modify: `cmake/shaders.cmake`, `CMakeLists.txt`
- Modify: `tests/test_draw.c`

**Interfaces:**
- Consumes: `BK_DrawCmd`'s field layout from Task 3 — `draw.vert`'s `Cmd` struct must match it byte for byte.
- Produces: a generated header `bk_draw_shaders.h` in the build tree, declaring `bk__draw_vertex_spv`, `bk__draw_vertex_spv_size`, `bk__draw_vertex_msl`, `bk__draw_vertex_msl_size`, and the four `fragment` equivalents. Task 5 includes it.

**Why embedding, not staged files.** Samples load their bytecode from a `shaders/` directory copied next to the binary by `bk_stage_shaders`. That is fine for sample-owned shaders, but `draw.vert`/`draw.frag` belong to the *framework*: a game linking `bielik` must not have to stage Bielik2D's own internals for `bk_draw_box_fill` to work. `#embed` would be the C23 answer, but `CLAUDE.md` reserves it for a later phase, so a CMake generator over the committed bytecode does the same job. This fills a gap the spec left open — §5.0 says `bk__draw_init` owns the compiled shaders without saying how the bytecode reaches it.

- [ ] **Step 1: Write the failing test**

Append to `tests/test_draw.c` and add to `main`:

```c
static void test_embedded_shader_bytecode_is_present(void) {
  // Guards the CMake generator: a mis-wired embed step yields empty arrays, and the
  // pipeline would then fail at init with a far less obvious message.
  REQUIRE(bk__draw_vertex_spv_size > 0);
  REQUIRE(bk__draw_fragment_spv_size > 0);
  // SPIR-V's magic number, little-endian.
  REQUIRE(bk__draw_vertex_spv[0] == 0x03);
  REQUIRE(bk__draw_vertex_spv[1] == 0x02);
  REQUIRE(bk__draw_vertex_spv[2] == 0x23);
  REQUIRE(bk__draw_vertex_spv[3] == 0x07);
}
```

Add `#include "bk_draw_shaders.h"` to `tests/test_draw.c`'s include block, and drop the now-redundant `target_include_directories` line for the generated directory if the `PUBLIC` inheritance in step 5 already covers it.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --target test_draw`
Expected: FAIL — `'bk_draw_shaders.h' file not found`.

- [ ] **Step 3: Write `shaders/draw.vert`**

```glsl
#version 450

layout (location = 0) in float in_corner;

struct Cmd {
  uvec4 meta;  // type, color_rg, payload offset, matrix palette offset
  vec4 shape;  // radius, half-stroke, antialias (world units), unused
  vec4 misc;   // fill, color_ba as float bits, unused, unused
};

layout (std430, set = 0, binding = 0) readonly buffer cmd_buffer { Cmd cmds[]; };
layout (std430, set = 0, binding = 1) readonly buffer payload_buffer { vec4 payload[]; };

layout (location = 0) out vec4 v_pos_uv;   // world pos.xy, uv.zw
layout (location = 1) out vec4 v_ab;       // payload P0
layout (location = 2) out vec4 v_cd;       // payload P1
layout (location = 3) out vec4 v_col;      // premultiplied colour
layout (location = 4) out vec4 v_shape;    // radius, half-stroke, aa, type
layout (location = 5) out float v_fill;

vec4 unpack_half4(uint rg, uint ba) {
  return vec4(unpackHalf2x16(rg), unpackHalf2x16(ba));
}

void main() {
  Cmd cmd = cmds[gl_InstanceIndex];
  uint type = cmd.meta.x;
  uint po = cmd.meta.z;
  int corner = int(in_corner + 0.5);

  vec4 P0 = payload[po];
  vec4 P1 = payload[po + 1u];

  // Corner 0 = (0,0), 1 = (1,0), 2 = (1,1), 3 = (0,1).
  float cx = (corner == 1 || corner == 2) ? 1.0 : 0.0;
  float cy = (corner == 2 || corner == 3) ? 1.0 : 0.0;

  // Conservative coverage inflation: the SDF band can extend this far past the
  // nominal shape, so the quad must too or the antialias edge gets clipped.
  float pad = cmd.shape.x + cmd.shape.y * 2.0 + cmd.shape.z;

  vec2 pos = vec2(0.0);
  vec2 uv = vec2(0.0);

  if (type == 0u) {
    // TEXTURE: exact destination box, no padding -- edges come from the sampler.
    pos = vec2(mix(P0.x, P0.z, cx), mix(P0.y, P0.w, cy));
    uv = vec2(mix(P1.x, P1.z, cx), mix(P1.y, P1.w, cy));
  } else if (type == 1u) {
    // BOX: centre plus padded half-extents. No rotation basis -- rotation lives in
    // the per-command matrix, so the SDF always sees an axis-aligned box.
    vec2 centre = P0.xy;
    vec2 he = P0.zw + vec2(pad);
    pos = centre + he * vec2(cx * 2.0 - 1.0, cy * 2.0 - 1.0);
  } else if (type == 2u) {
    // CIRCLE: padded square around the centre.
    vec2 he = vec2(cmd.shape.x + pad);
    pos = P0.xy + he * vec2(cx * 2.0 - 1.0, cy * 2.0 - 1.0);
  } else if (type == 3u) {
    // SEGMENT: padded AABB of the two endpoints.
    vec2 mn = min(P0.xy, P0.zw) - vec2(pad);
    vec2 mx = max(P0.xy, P0.zw) + vec2(pad);
    pos = vec2(mix(mn.x, mx.x, cx), mix(mn.y, mx.y, cy));
  } else if (type == 4u) {
    // TRI: padded AABB of the three vertices.
    vec2 mn = min(P0.xy, min(P0.zw, P1.xy)) - vec2(pad);
    vec2 mx = max(P0.xy, max(P0.zw, P1.xy)) + vec2(pad);
    pos = vec2(mix(mn.x, mx.x, cx), mix(mn.y, mx.y, cy));
  } else {
    // ARROW: padded AABB of the endpoints; the head fits within max(radius, width).
    float apad = max(P1.x, P1.y) + pad;
    vec2 mn = min(P0.xy, P0.zw) - vec2(apad);
    vec2 mx = max(P0.xy, P0.zw) + vec2(apad);
    pos = vec2(mix(mn.x, mx.x, cx), mix(mn.y, mx.y, cy));
  }

  // Forward MVP from the matrix palette: two vec4s, basis then origin.
  vec4 f0 = payload[cmd.meta.w];
  vec4 f1 = payload[cmd.meta.w + 1u];
  vec2 posH = f0.xy * pos.x + f0.zw * pos.y + f1.xy;

  v_pos_uv = vec4(pos, uv);
  v_ab = P0;
  v_cd = P1;
  v_col = unpack_half4(cmd.meta.y, floatBitsToUint(cmd.misc.y));
  v_shape = vec4(cmd.shape.x, cmd.shape.y, cmd.shape.z, float(type));
  v_fill = cmd.misc.x;
  gl_Position = vec4(posH, 0.0, 1.0);
}
```

- [ ] **Step 4: Write `shaders/draw.frag`**

The SDF helpers are transliterated from Cute Framework's `s_sdf_core` and `s_distance` (`tools/builtin_shaders.h`), zlib/public-domain. `distance_segment`, `distance_triangle` and `distance_arrow` carry their original shadertoy references.

```glsl
#version 450

layout (location = 0) in vec4 v_pos_uv;
layout (location = 1) in vec4 v_ab;
layout (location = 2) in vec4 v_cd;
layout (location = 3) in vec4 v_col;
layout (location = 4) in vec4 v_shape;
layout (location = 5) in float v_fill;

layout (location = 0) out vec4 result;

layout (set = 2, binding = 0) uniform sampler2D u_image;

float safe_div(float a, float b) { return b == 0.0 ? 0.0 : a / b; }
float safe_len(vec2 v) { float d = dot(v, v); return d == 0.0 ? 0.0 : sqrt(d); }
float det2(vec2 a, vec2 b) { return a.x * b.y - a.y * b.x; }

float distance_aabb(vec2 p, vec2 he) {
  vec2 d = abs(p) - he;
  return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
}

// Referenced from: https://www.shadertoy.com/view/3tdSDj
float distance_segment(vec2 p, vec2 a, vec2 b) {
  vec2 n = b - a;
  vec2 pa = p - a;
  float d = safe_div(dot(pa, n), dot(n, n));
  float h = clamp(d, 0.0, 1.0);
  return safe_len(pa - h * n);
}

// Referenced from: https://www.shadertoy.com/view/XsXSz4
float distance_triangle(vec2 p, vec2 a, vec2 b, vec2 c) {
  vec2 e0 = b - a, e1 = c - b, e2 = a - c;
  vec2 v0 = p - a, v1 = p - b, v2 = p - c;
  vec2 pq0 = v0 - e0 * clamp(safe_div(dot(v0, e0), dot(e0, e0)), 0.0, 1.0);
  vec2 pq1 = v1 - e1 * clamp(safe_div(dot(v1, e1), dot(e1, e1)), 0.0, 1.0);
  vec2 pq2 = v2 - e2 * clamp(safe_div(dot(v2, e2), dot(e2, e2)), 0.0, 1.0);
  float s = det2(e0, e2);
  vec2 d = min(min(vec2(dot(pq0, pq0), s * det2(v0, e0)),
                   vec2(dot(pq1, pq1), s * det2(v1, e1))),
                   vec2(dot(pq2, pq2), s * det2(v2, e2)));
  return -sqrt(d.x) * sign(d.y);
}

// Shaft capsule unioned with a triangular head, as one SDF so the seam never
// double-blends. r = shaft radius, w = head length and half-width.
float distance_arrow(vec2 p, vec2 a, vec2 b, float r, float w) {
  vec2 d = b - a;
  float l = safe_len(d);
  vec2 n = l == 0.0 ? vec2(0.0) : d / l;
  vec2 base = b - n * w;
  vec2 t = vec2(-n.y, n.x) * w;
  float ds = distance_segment(p, a, base) - r;
  float dt = distance_triangle(p, b, base + t, base - t);
  return min(ds, dt);
}

void main() {
  uint type = uint(v_shape.w + 0.5);
  vec2 pos = v_pos_uv.xy;
  float radius = v_shape.x;
  float half_stroke = v_shape.y;
  float aa = v_shape.z;

  if (type == 0u) {
    result = texture(u_image, v_pos_uv.zw) * v_col;
    return;
  }

  float dist;
  if (type == 1u) {
    dist = distance_aabb(pos - v_ab.xy, v_ab.zw - vec2(radius)) - radius;
  } else if (type == 2u) {
    dist = safe_len(pos - v_ab.xy) - radius;
  } else if (type == 3u) {
    dist = distance_segment(pos, v_ab.xy, v_ab.zw) - half_stroke;
  } else if (type == 4u) {
    dist = distance_triangle(pos, v_ab.xy, v_ab.zw, v_cd.xy) - radius;
  } else {
    dist = distance_arrow(pos, v_ab.xy, v_ab.zw, v_cd.x, v_cd.y);
  }

  // A stroked shape is the shell around its own isosurface.
  if (v_fill < 0.5) {
    dist = abs(dist) - half_stroke;
  }

  // aa arrives in world units (the CPU divided the pixel band by the composed scale),
  // which is the space dist is measured in.
  float coverage = aa > 0.0 ? 1.0 - smoothstep(-aa, aa, dist) : (dist <= 0.0 ? 1.0 : 0.0);
  if (coverage <= 0.0) {
    discard;
  }
  result = v_col * coverage;
}
```

- [ ] **Step 5: Wire up compilation and embedding**

Add to `CMakeLists.txt`, beside the existing `bk_compile_shader` calls (around line 51). **No `MSL_DECORATION_BINDING`**: that remap is only needed when one stage declares both a storage buffer and a uniform buffer, and `draw.vert` declares two storage buffers and zero uniform buffers.

```cmake
bk_compile_shader(NAME draw STAGE vertex)
bk_compile_shader(NAME draw STAGE fragment)
```

Create `cmake/embed_shader.cmake` — a script run by `cmake -P` that reads one binary file and writes a C array:

```cmake
# Usage: cmake -DBK_NAME=<shader> -DBK_SHADER_DIR=<dir> -DBK_OUT=<header> -P embed_shader.cmake
#
# Turns four committed bytecode files into one C header of byte arrays. [[maybe_unused]]
# because the header is included by both src/bk_draw.c and tests/test_draw.c, and
# neither reads every array -- -Werror would reject the unused ones otherwise.
set(body "// Generated from ${BK_SHADER_DIR}/${BK_NAME}.*. Do not edit.\n#pragma once\n")
foreach(stage vertex fragment)
    foreach(format spv msl)
        set(path "${BK_SHADER_DIR}/${BK_NAME}.${stage}.${format}")
        file(READ "${path}" hex HEX)
        string(REGEX MATCHALL "([A-Fa-f0-9][A-Fa-f0-9])" bytes "${hex}")
        list(LENGTH bytes count)
        string(REPLACE ";" ", 0x" joined "${bytes}")
        set(symbol "bk__${BK_NAME}_${stage}_${format}")
        string(APPEND body
            "[[maybe_unused]] static const unsigned char ${symbol}[] = { 0x${joined} };\n"
            "[[maybe_unused]] static const unsigned long ${symbol}_size = ${count};\n")
    endforeach()
endforeach()
file(WRITE "${BK_OUT}" "${body}")
```

Note the symbol names this produces: `bk__draw_vertex_spv`, `bk__draw_vertex_spv_size`, `bk__draw_vertex_msl`, `bk__draw_fragment_spv`, and so on — matching what Task 5's pipeline descriptor uses. The `.msl` files are text, not binary, but `file(READ ... HEX)` handles both identically and SDL wants a byte pointer plus a size either way.

Add to `cmake/shaders.cmake`:

```cmake
# Compiles the committed bytecode for NAME into a C header of byte arrays, so the
# framework's own shaders need no runtime files staged beside a consumer's binary.
# Regenerates whenever the bytecode changes.
function(bk_embed_shader)
    set(one_value_args NAME)
    cmake_parse_arguments(ARG "" "${one_value_args}" "" ${ARGN})
    set(out "${CMAKE_BINARY_DIR}/generated/bk_${ARG_NAME}_shaders.h")
    set(inputs
        "${PROJECT_SOURCE_DIR}/shaders/${ARG_NAME}.vertex.spv"
        "${PROJECT_SOURCE_DIR}/shaders/${ARG_NAME}.vertex.msl"
        "${PROJECT_SOURCE_DIR}/shaders/${ARG_NAME}.fragment.spv"
        "${PROJECT_SOURCE_DIR}/shaders/${ARG_NAME}.fragment.msl")
    add_custom_command(
        OUTPUT "${out}"
        COMMAND ${CMAKE_COMMAND}
                -DBK_NAME=${ARG_NAME}
                -DBK_SHADER_DIR=${PROJECT_SOURCE_DIR}/shaders
                -DBK_OUT=${out}
                -P "${PROJECT_SOURCE_DIR}/cmake/embed_shader.cmake"
        DEPENDS ${inputs} "${PROJECT_SOURCE_DIR}/cmake/embed_shader.cmake"
        COMMENT "Embedding ${ARG_NAME} shader bytecode -> bk_${ARG_NAME}_shaders.h"
        VERBATIM)
    add_custom_target(bk_embed_${ARG_NAME} DEPENDS "${out}")
endfunction()
```

Then, in `CMakeLists.txt`:

```cmake
bk_embed_shader(NAME draw)
add_dependencies(bielik bk_embed_draw)
target_include_directories(bielik PUBLIC "${CMAKE_BINARY_DIR}/generated")
```

`PUBLIC`, not `PRIVATE`: `tests/test_draw.c` includes the generated header too, and inherits the directory through its `bielik` link.


- [ ] **Step 6: Run to verify it passes**

Run: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBK_WERROR=ON && cmake --build build --target test_draw && ctest --test-dir build -R "^test_draw$" --output-on-failure`
Expected: PASS.

If `glslc`/`spirv-cross` are not installed, `bk_compile_shader` logs and skips — the committed `.spv`/`.msl` are used instead. Since `draw.vertex.spv` does not exist yet on the first run, install both tools before this step (`brew install shaderc spirv-cross` on macOS) and commit the generated bytecode.

- [ ] **Step 7: Format and commit**

```bash
cmake --build build --target format
git add shaders/draw.vert shaders/draw.frag shaders/draw.vertex.spv shaders/draw.vertex.msl \
        shaders/draw.fragment.spv shaders/draw.fragment.msl \
        cmake/embed_shader.cmake cmake/shaders.cmake CMakeLists.txt \
        tests/test_draw.c tests/CMakeLists.txt
git commit -m "add the bk_draw SDF shaders and embed their bytecode"
```

---

### Task 5: Init, collate, and the first pixels

**Files:**
- Modify: `src/internal/bk_gfx_texture_internal.h`, `src/bk_gfx_texture.c`
- Modify: `src/internal/bk_draw_internal.h`, `src/bk_draw.c`
- Modify: `src/bk_app.c`
- Create: `tests/test_draw_gpu.c`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `bk__draw_pack` from Task 3; the embedded bytecode from Task 4; `bk_gfx_pipeline_create`, `bk_gfx_buffer_create`, `bk_gfx_buffer_upload`, `bk_gfx_bind_*`, `bk_gfx_draw_instanced` from `bk_gfx`; `bk_window_size`, `bk_gpu`, `bk_window` from `bielik/bk_app.h`; `bk__gfx_get_pending_canvas`, `bk__gfx_canvas_depth_format` from the gfx internals.
- Produces: `void bk__draw_init(void)`, `void bk__draw_collate(void)`, `void bk__draw_shutdown(void)`, and `bool bk__gfx_texture_size(const BK_GfxTexture *texture, i32 *out_w, i32 *out_h)`.

- [ ] **Step 1: Write the failing test**

Create `tests/test_draw_gpu.c`. **Open `tests/test_gfx_drawlist_gpu.c` first** — it already solves booting a real app, capturing one frame, and probing pixels, and this file reuses that machinery rather than reinventing it. Copy across:

- its `BK_AppDesc` construction and single-frame `bk_run` (`app_update` returns `BK_DONE` after one frame);
- its `bk_gfx_request_capture(path)` call in `app_render`, writing to a temp path;
- its `SDL_LoadBMP` of that path, then `SDL_ConvertSurface` to `SDL_PIXELFORMAT_RGBA32` — the `_32` alias, **not** `_8888`, which is bit-packed order and flips on little-endian (`CLAUDE.md`);
- its `printf(... "no capture produced ..."); return;` early-out when the capture is missing, so the test degrades to a skip rather than a failure on a machine with no usable GPU.

Write one helper both this task and Task 6 reuse, so each case is only its render callback plus its probes:

```c
// Boots a one-frame app with the given render callback, captures the frame, and hands
// back the RGBA32 surface. Returns nullptr if no capture was produced -- callers treat
// that as a skip, not a failure. Caller destroys the surface.
static SDL_Surface *s_render_one_frame(void (*render)(void *, const BK_FrameInfo *));

// Byte offset of the pixel at (x, y) in a converted surface.
static const u8 *s_pixel_at(const SDL_Surface *surface, int px, int py) {
  return (const u8 *)surface->pixels + (usize)py * (usize)surface->pitch + (usize)px * 4;
}
```

The first case:

```c
// A filled red box centred in the window: the centre pixel is red, a corner well
// outside it is the clear colour. Probes specific pixels rather than hashing the
// frame -- SDF antialiasing does not hash-match across Metal and Vulkan.
static void app_render_box(void *state, const BK_FrameInfo *frame) {
  (void)state;
  (void)frame;
  bk_draw_push_color((BK_Color){1.0f, 0.0f, 0.0f, 1.0f});
  bk_draw_box_fill(bk_aabb(bk_v2(-64.0f, -64.0f), bk_v2(64.0f, 64.0f)), 0.0f);
  bk_draw_pop_color();
}

static void test_filled_box_draws(void) {
  SDL_Surface *frame = s_render_one_frame(app_render_box);
  if (frame == nullptr) {
    printf("test_draw_gpu: no capture produced, skipping filled box\n");
    return;
  }

  const u8 *centre = s_pixel_at(frame, frame->w / 2, frame->h / 2);
  REQUIRE(centre[0] > 200); // red
  REQUIRE(centre[1] < 55);
  REQUIRE(centre[2] < 55);

  // Well outside the 128x128 box, so this is the clear colour.
  const u8 *corner = s_pixel_at(frame, 4, 4);
  REQUIRE(corner[0] < 55);

  SDL_DestroySurface(frame);
}
```

The window is 1280x720 (match `07_instanced`), and the box is 128x128 world units centred at the origin. With the default projection — `bk_m3x2_ortho(1280, 720)`, origin at the centre, one unit per pixel — the box lands dead centre, spanning pixels 576..704 horizontally. Every probe coordinate in this task and Task 6 follows from that mapping.

- [ ] **Step 2: Register it and run to verify it fails**

Add to `tests/CMakeLists.txt`, following `test_gfx_drawlist_gpu`'s shape:

```cmake
# GPU replay tests: needs a real device and pipeline, so it joins CI's allow-failure
# GPU group (no DXIL variant exists for Windows -- see DEVIATIONS.md).
add_executable(test_draw_gpu test_draw_gpu.c)
target_link_libraries(test_draw_gpu PRIVATE bielik bk_warnings)
target_include_directories(test_draw_gpu PRIVATE ${PROJECT_SOURCE_DIR}/src)
add_test(NAME test_draw_gpu COMMAND test_draw_gpu)
set_tests_properties(test_draw_gpu PROPERTIES TIMEOUT 60)
```

Run: `cmake --build build --target test_draw_gpu && ctest --test-dir build -R test_draw_gpu --output-on-failure`
Expected: FAIL — the centre pixel is the clear colour, because nothing is wired up yet.

- [ ] **Step 3: Add the texture size accessor**

In `src/internal/bk_gfx_texture_internal.h`:

```c
/// Writes texture's dimensions in texels to *out_w/*out_h. Framework-internal; used by
/// bk_draw to normalise a source rect into UVs and to size the default projection when
/// a canvas is bound. Returns false if texture is nullptr.
bool bk__gfx_texture_size(const BK_GfxTexture *texture, i32 *out_w, i32 *out_h);
```

Implement it in `src/bk_gfx_texture.c` by reading the width/height already stored in the internal `BK_GfxTexture` struct.

- [ ] **Step 4: Implement init, collate, and shutdown**

Declare all three in `src/internal/bk_draw_internal.h` with doc comments, then implement in `src/bk_draw.c`.

**`bk__draw_init`** creates, in this order:

1. The **corner buffer** — `BK_GFX_BUFFER_USAGE_VERTEX`, six floats `{0, 1, 2, 0, 2, 3}`, uploaded once. Two triangles' worth of corner indices; `draw.vert` reads it as `in_corner`.
2. Both **pipelines**. Two, not one, because `bk_gfx.h:80-83` asserts a bound pipeline's `depth_stencil_format` equals the render pass's attachment format, and a game may bind a depth-enabled canvas or set `BK_AppDesc.window.depth_stencil` before calling `bk_draw_*`. The reachable set is exactly `{SDL_GPU_TEXTUREFORMAT_INVALID, bk_gfx_depth_stencil_format(bk_gpu())}` because that function probes and returns one format per device. Both are otherwise identical:

```c
BK_GfxVertexBufferLayout layout = {.slot = 0, .pitch = sizeof(f32)};
BK_GfxVertexAttribute attribute = {
    .location = 0, .buffer_slot = 0, .format = BK_GFX_VERTEX_FORMAT_FLOAT, .offset = 0};

BK_GfxPipelineDesc desc = {
    .vertex_shader = {.spirv = {bk__draw_vertex_spv, bk__draw_vertex_spv_size, "main"},
                      .msl = {bk__draw_vertex_msl, bk__draw_vertex_msl_size, "main0"},
                      .num_storage_buffers = 2},
    .fragment_shader = {.spirv = {bk__draw_fragment_spv, bk__draw_fragment_spv_size, "main"},
                        .msl = {bk__draw_fragment_msl, bk__draw_fragment_msl_size, "main0"},
                        .num_samplers = 1},
    .vertex_buffers = &layout,
    .num_vertex_buffers = 1,
    .vertex_attributes = &attribute,
    .num_vertex_attributes = 1,
    .primitive_type = BK_GFX_PRIMITIVE_TRIANGLE_LIST,
    .color_target_format = SDL_GetGPUSwapchainTextureFormat(bk_gpu(), bk_window()),
    .blend_mode = BK_GFX_BLEND_PREMULTIPLIED,
    .depth_stencil_format = SDL_GPU_TEXTUREFORMAT_INVALID, // and the depth variant
};
```

**`BK_GFX_VERTEX_FORMAT_FLOAT` does not exist yet** — `bk_gfx_pipeline.h:33-38` has `FLOAT2`, `FLOAT3`, `FLOAT4`, `UBYTE4_NORM`. Add a `BK_GFX_VERTEX_FORMAT_FLOAT` value and its `SDL_GPU_VERTEXELEMENTFORMAT_FLOAT` mapping in `src/bk_gfx_pipeline.c`. Append it to the end of the enum, not the middle — the existing values are used by four samples and two tests.

Also note the resource counts above: vertex declares 2 storage buffers and **0** uniform buffers; fragment declares 1 sampler and 0 of everything else. `CLAUDE.md` records that a mismatch here does not fail at pipeline creation — it drops the command buffer at draw time, and the target reads back all-zero with nothing logged. If step 6 produces a blank frame, check these first.

3. A **default sampler**, linear filter, clamp-to-edge, for `bk_draw_texture`.

**`bk__draw_collate`**:

```c
void bk__draw_collate(void) {
  i32 target_w = 0, target_h = 0;
  BK_GfxCanvas *canvas = bk__gfx_get_pending_canvas();
  if (canvas != nullptr) {
    bk__gfx_texture_size(bk_gfx_canvas_texture(canvas), &target_w, &target_h);
  } else {
    // Not the swapchain texture's size: bk_gfx only learns that from
    // SDL_WaitAndAcquireGPUSwapchainTexture inside bk__gfx_flush, which runs after
    // this. bk_window_size is cached and refreshed on resize before any app handler.
    bk_window_size(&target_w, &target_h);
  }

  BK_DrawPacked packed = {0};
  if (bk__draw_pack(&packed, target_w, target_h)) {
    // ... create + upload the two storage buffers, emit one draw per batch ...
  }
  bk__draw_reset();
}
```

Per batch, emit: `bk_gfx_bind_pipeline` (the depth variant chosen from `bk__gfx_canvas_depth_format(canvas)`), `bk_gfx_bind_vertex_buffer(corner_buffer)`, `bk_gfx_bind_vertex_storage_buffer(cmds, 0)`, `bk_gfx_bind_vertex_storage_buffer(payload, 1)`, `bk_gfx_bind_texture(batch->texture, sampler)` when the batch has one, `bk_gfx_set_scissor(batch->scissor)`, then `bk_gfx_draw_instanced(6, batch->count)`.

The two storage buffers are created and destroyed per frame. That is one allocation pair per frame and is deliberately the simple thing; a persistent cycled buffer is P3.6's concern.

**`bk__draw_shutdown`** destroys both pipelines, the sampler, and the corner buffer, and nulls them.

**`bk_app.c` edits** — three calls:
- `bk__draw_init()` after the existing `bk__gfx_configure_swapchain_depth(...)` call at line 214.
- `bk__draw_collate()` between `s_app.desc.render(appstate, &info)` and `bk__gfx_flush()` at line 281.
- `bk__draw_shutdown()` immediately before `bk__gfx_shutdown()` at line 317.

- [ ] **Step 5: Run to verify it passes**

Run: `cmake --build build && ctest --test-dir build -R "test_draw" --output-on-failure`
Expected: both `test_draw` and `test_draw_gpu` PASS.

- [ ] **Step 6: Format and commit**

```bash
cmake --build build --target format
git add src/bk_draw.c src/internal/bk_draw_internal.h src/bk_gfx_texture.c \
        src/internal/bk_gfx_texture_internal.h src/bk_gfx_pipeline.c \
        include/bielik/bk_gfx_pipeline.h src/bk_app.c tests/test_draw_gpu.c tests/CMakeLists.txt
git commit -m "draw the bk_draw record chain through bk_gfx"
```

---

### Task 6: The rest of the shapes, on the GPU

**Files:**
- Modify: `tests/test_draw_gpu.c`

**Interfaces:**
- Consumes: everything from Tasks 2–5. No new production code is expected — the shapes are already implemented; this task proves each one renders and fixes whatever it finds.

- [ ] **Step 1: Write the failing tests**

Add one case per remaining shape to `tests/test_draw_gpu.c`, each following `test_filled_box_draws`'s structure. For every shape assert: an interior pixel carries the fill colour, and a pixel well outside is the clear colour. For the stroked variants additionally assert the centre is clear.

- `test_filled_circle_draws` — `bk_draw_circle_fill(bk_v2(0, 0), 64.0f)`; centre filled, `(0, 100)` outside the radius clear.
- `test_stroked_circle_is_hollow` — `bk_draw_circle(bk_v2(0, 0), 64.0f, 8.0f)`; centre **clear**, a pixel on the ring filled.
- `test_line_draws` — `bk_draw_line(bk_v2(-64, 0), bk_v2(64, 0), 8.0f)`; a pixel on the segment filled, one 40px above it clear.
- `test_filled_tri_draws` — `bk_draw_tri_fill(bk_v2(0, 64), bk_v2(-64, -64), bk_v2(64, -64), 0.0f)`; the centroid filled, a top corner of the AABB clear.
- `test_arrow_draws` — `bk_draw_arrow(bk_v2(-64, 0), bk_v2(64, 0), 6.0f, 20.0f)`; a shaft pixel filled, one well above the head clear.
- `test_rounded_box_corner_is_cut` — `bk_draw_box_fill(bb, 24.0f)`; the box's exact corner pixel clear, its centre filled.
- `test_texture_draws` — create a 2x2 texture with one known non-uniform colour per texel, `bk_draw_texture` it across the window, and assert two pixels in different quadrants carry different colours. This is what proves `src_px` → UV normalisation is right, which no shape test touches.
- `test_layers_reorder_paint_order` — draw a red box at layer 5 **first**, then a blue box covering the same area at layer 0. Assert the centre is **red**: the later-recorded blue box paints first because its layer is lower.
- `test_scissor_clips` — a box covering the window, inside `bk_draw_push_scissor` over the left half. Assert a left-half pixel is filled and a right-half pixel is clear.

- [ ] **Step 2: Run to verify they fail**

Run: `cmake --build build --target test_draw_gpu && ctest --test-dir build -R test_draw_gpu --output-on-failure`
Expected: FAIL on at least one case. Any that pass immediately are fine — they were already correct.

- [ ] **Step 3: Fix what the tests find**

Likely candidates, in the order they usually bite:

- **Sign conventions.** `distance_triangle` returns `-sqrt(d.x) * sign(d.y)`; confirm negative means inside, matching `distance_aabb`. If a filled triangle renders inside-out, negate it.
- **Stroke on unfilled shapes.** `bk_draw_circle` sets `half_stroke` and `fill = false`, so the fragment shader takes the `abs(dist) - half_stroke` branch. A solid circle where a ring was expected means `fill` reached the shader as 1.
- **Coverage padding.** A clipped antialias edge or a shape cut off at its bounding box means `pad` in `draw.vert` is too small for that type.
- **UV normalisation.** `bk_draw_texture` must divide `src_px` by the texture's dimensions from `bk__gfx_texture_size`. Passing texels straight through gives a single-texel-coloured quad.

- [ ] **Step 4: Run to verify they pass**

Run: `ctest --test-dir build -R "test_draw" --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Format and commit**

```bash
cmake --build build --target format
git add tests/test_draw_gpu.c src/bk_draw.c shaders/
git commit -m "prove every bk_draw shape renders"
```

---

### Task 7: The sample

**Files:**
- Create: `samples/08_draw/main.c`, `samples/08_draw/CMakeLists.txt`
- Modify: `samples/CMakeLists.txt`

**Interfaces:**
- Consumes: the entire public API. Produces nothing other code depends on.

- [ ] **Step 1: Write the sample**

Create `samples/08_draw/main.c`. Follow `samples/07_instanced/main.c`'s structure exactly: the `AppState` struct with `frame_count`/`frame_limit`, the `--frames N` argument parse in `app_init`, the `app_event` handler returning `BK_DONE` on quit and Escape, and the `#ifdef BK_MAIN_HANDLED` / `BK_APP(...)` tail.

It needs **no shader loading and no pipeline** — that is the point of the sample, and worth a comment saying so.

`app_render` must exercise, with a comment naming what each block demonstrates:
- every shape in the set, filled and stroked, laid out in a readable row;
- a rotating `bk_draw_push` / `bk_draw_rotate` / `bk_draw_pop` around one group, so the camera stack is visibly doing work;
- two overlapping boxes whose layers invert their record order;
- the same shape drawn twice side by side under `bk_draw_push_antialias(1.0f)` and `bk_draw_push_antialias(0.0f)`, so the pixel-art path is visible.

- [ ] **Step 2: Register it**

Create `samples/08_draw/CMakeLists.txt`, copying `samples/07_instanced/CMakeLists.txt` **minus** the `bk_stage_shaders` calls — `bk_draw`'s bytecode is compiled into the library:

```cmake
add_executable(08_draw main.c)
target_link_libraries(08_draw PRIVATE bielik bk_warnings)

add_executable(08_draw_run main.c)
target_link_libraries(08_draw_run PRIVATE bielik bk_warnings)
target_compile_definitions(08_draw_run PRIVATE BK_MAIN_HANDLED)
```

Add `add_subdirectory(08_draw)` to `samples/CMakeLists.txt`.

- [ ] **Step 3: Run it**

Run: `cmake --build build --target 08_draw && ./build/samples/08_draw/08_draw --frames 120`
Expected: a window showing every shape, exits after 120 frames with status 0.

- [ ] **Step 4: Format and commit**

```bash
cmake --build build --target format
git add samples/08_draw samples/CMakeLists.txt
git commit -m "add the 08_draw sample"
```

---

### Task 8: CI registration and documentation

**Files:**
- Modify: `.github/workflows/ci.yml`
- Modify: `DEVIATIONS.md`, `NOTICE.md`, `PLAN.md`
- Modify: `docs/superpowers/specs/2026-08-02-bk-draw-design.md`

**Interfaces:**
- Consumes: the finished module. Produces nothing.

- [ ] **Step 1: Put `test_draw_gpu` in the GPU group**

`ci.yml` selects the GPU group with an explicit `-R`/`-E` alternation repeated on **six** lines (70, 75, 80, 124, 129, 134). Add `|test_draw_gpu` to every one. Miss one and `test_draw_gpu` silently becomes a required check on a machine with no GPU.

`test_draw` needs no change: it is CPU-only, so it is picked up by the `-E` legs automatically, which is exactly the intent.

Run: `grep -c "test_draw_gpu" .github/workflows/ci.yml`
Expected: `6`.

- [ ] **Step 2: Record the deviations**

Add to `DEVIATIONS.md`:

- **Issue #21 reassigned from P3.3 to P3.6.** The P3.2 spec §5 assigned the singular-transform inverse-MVP guard to P3.3. CF's instanced path never builds an inverse — world space reaches the fragment shader as an interpolated varying — so the hazard first arises in P3.6's tiled path. P3.3's record-time cull is a cull, not that guard. [Issue #21](https://github.com/pusewicz/bielik2d-c/issues/21) is retargeted.
- **`bk_draw` shader bytecode is embedded by CMake, not `#embed`.** `CLAUDE.md` reserves `#embed` for a later phase, but a framework cannot require consumers to stage its internal shaders next to their binary. `cmake/embed_shader.cmake` generates C arrays from the committed bytecode. Revisit when `#embed` is unblocked.
- **`BK_GFX_VERTEX_FORMAT_FLOAT` added** to `bk_gfx_pipeline.h`'s enum, appended rather than inserted, for `draw.vert`'s single-float corner attribute.

- [ ] **Step 3: Add the zlib attribution**

`PLAN.md` §7 requires it for the CF port. Add to `NOTICE.md` a section naming Cute Framework, its zlib/public-domain dual licence, and what was transliterated: the SDF distance functions in `shaders/draw.frag` (`distance_aabb`, `distance_segment`, `distance_triangle`, `distance_arrow`) and the instanced command-renderer design in `shaders/draw.vert`.

- [ ] **Step 4: Correct the spec against what was built**

Update `docs/superpowers/specs/2026-08-02-bk-draw-design.md` where implementation diverged:
- §5's collate step says `SDL_GetWindowSizeInPixels(bk_window())`; the implementation uses the existing public `bk_window_size()`, which is cached and refreshed on resize before any app handler runs.
- §5.0 does not say how shader bytecode reaches `bk__draw_init`; add a sentence naming the CMake embed step.
- §1's File layout gains `cmake/embed_shader.cmake`, `src/bk_gfx_pipeline.c`, and `include/bielik/bk_gfx_pipeline.h`.

Also update `CLAUDE.md`'s Phase status paragraph: P3.3 has landed, and P3.4 (atlas) is next.

- [ ] **Step 5: Verify the whole suite and commit**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBK_WERROR=ON && cmake --build build
ctest --test-dir build --output-on-failure
cmake --build build --target format-check
```

Expected: every test passes and `format-check` is clean.

```bash
git add .github/workflows/ci.yml DEVIATIONS.md NOTICE.md PLAN.md CLAUDE.md docs/
git commit -m "register the bk_draw tests in CI and record its deviations"
```
