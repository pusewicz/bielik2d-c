# Notice

Third-party attributions are recorded here as code is ported from other projects.

## Cute Framework

Bielik2D's `bk_draw` module (Phase 3, sub-project 3) ports parts of
[Cute Framework](https://github.com/RandyGaul/cute_framework) by Randy Gaul, dual-licensed
zlib / public domain (see CF's own `LICENSE`). Ported from a checkout at
`/Users/piotr/Work/GitHub/pusewicz/cute_framework`:

- The SDF distance functions in `shaders/draw.frag` — `distance_aabb`, `distance_segment`,
  `distance_triangle`, `distance_arrow`, and the `safe_div`/`safe_len`/`det2` helpers they
  build on — transliterated from CF's `s_sdf_core` and `s_distance`
  (`tools/builtin_shaders.h`). Individual shadertoy attributions CF itself carries for some
  of these functions are kept in place in `draw.frag`.
- The instanced command-renderer design in `shaders/draw.vert` — one vertex invocation per
  shape instance deriving a coverage quad from packed command data, evaluated as a signed
  distance field in the fragment stage — transliterated from CF's `s_inst_vs`.

Per zlib clause 1, this acknowledgment stands in place of claiming original authorship of
the ported logic; per clause 2, the port is altered (rewritten for SDL_GPU/GLSL and
Bielik2D's command layout, not copied verbatim) and is not represented as CF's original
source.
