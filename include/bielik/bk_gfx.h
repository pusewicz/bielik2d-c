#pragma once
#include <bielik/bk_math.h> // BK_Color
#include <bielik/bk_types.h>

typedef struct BK_GfxPipeline BK_GfxPipeline;
typedef struct BK_GfxBuffer BK_GfxBuffer;
typedef struct BK_GfxTexture BK_GfxTexture;
typedef struct BK_GfxSampler BK_GfxSampler;
typedef struct BK_GfxCanvas BK_GfxCanvas;

/// Sets the color the swapchain is cleared to each frame.
void bk_gfx_set_clear_color(BK_Color color);

/// Binds a pipeline for every subsequent draw this frame. Bindings are sticky: they
/// apply to each bk_gfx_draw* recorded after them, until changed or until the frame's
/// flush clears them.
void bk_gfx_bind_pipeline(BK_GfxPipeline *pipeline);

/// Records a draw of vertex_count vertices, capturing the state bound above it. The
/// frame's flush replays every recorded draw in call order.
void bk_gfx_draw(i32 vertex_count);

/// Binds a vertex buffer to slot 0 for every subsequent draw this frame.
void bk_gfx_bind_vertex_buffer(BK_GfxBuffer *buffer);

/// Binds an index buffer for every subsequent indexed draw this frame. Indices are
/// always read as 16-bit (see bk_gfx_buffer.h's BK_GFX_BUFFER_USAGE_INDEX).
void bk_gfx_bind_index_buffer(BK_GfxBuffer *buffer);

/// Binds a texture and sampler pair to fragment slot 0 for every subsequent draw this
/// frame.
void bk_gfx_bind_texture(BK_GfxTexture *texture, BK_GfxSampler *sampler);

/// Records an indexed draw of index_count indices, using the bound index buffer and
/// capturing the state bound above it.
void bk_gfx_draw_indexed(i32 index_count);

/// Binds a storage buffer the vertex shader reads, at the given slot (0-based, four
/// slots per stage). buffer must have been created with
/// BK_GFX_BUFFER_USAGE_STORAGE_GRAPHICS, and the pipeline's vertex shader desc must
/// declare a matching num_storage_buffers -- a mismatch there fails silently at draw
/// time, not at pipeline creation. Applies to every subsequent draw this frame.
void bk_gfx_bind_vertex_storage_buffer(BK_GfxBuffer *buffer, i32 slot);

/// Binds a storage buffer the fragment shader reads, at the given slot. Same
/// requirements as bk_gfx_bind_vertex_storage_buffer.
void bk_gfx_bind_fragment_storage_buffer(BK_GfxBuffer *buffer, i32 slot);

/// Sets size bytes as vertex uniform slot 0 for every subsequent draw this frame. The
/// data is copied into the frame arena, so a stack local is safe -- the GPU push
/// happens later, at flush, when the caller's frame is long gone. Must respect std140
/// layout (vec3/vec4 members 16-byte aligned): SDL_GPU pushes the bytes verbatim. The
/// pipeline's vertex shader desc must declare num_uniform_buffers >= 1.
void bk_gfx_push_vertex_uniform(const void *data, u32 size);

/// Fragment-stage equivalent of bk_gfx_push_vertex_uniform, at fragment uniform slot 0.
void bk_gfx_push_fragment_uniform(const void *data, u32 size);

/// Records an instanced draw: instance_count copies of vertex_count vertices. The
/// vertex shader reads gl_InstanceIndex (SV_InstanceID) to pick per-instance data,
/// typically out of a bound vertex storage buffer. Instances are always numbered from
/// 0 within this draw -- there is no first_instance parameter here, and there
/// deliberately never will be: SDL_gpu.h documents first_instance as incompatible with
/// shader built-in instance IDs and says to always pass 0, because SDL forwards it to
/// each backend unnormalized and HLSL's SV_InstanceID is zero-based where Vulkan's,
/// Metal's, and WebGPU's equivalents fold the offset in. A caller slicing one shared
/// buffer across several draws (as bk_draw's batches do) must push the base offset as
/// a uniform instead -- see BK_DrawBatchUniform in src/bk_draw.c for a worked example.
void bk_gfx_draw_instanced(i32 vertex_count, i32 instance_count);

/// Clips every subsequent draw this frame to rect, in pixels of the render target with
/// a top-left origin -- the bound canvas's dimensions when one is bound, the
/// swapchain's otherwise, NOT the window's. A width or height <= 0 means "no scissor"
/// (the full target), which is also the default, so passing a zero rect resets it.
void bk_gfx_set_scissor(BK_Rect rect);

/// Maps every subsequent draw this frame onto rect, in pixels of the render target with
/// a top-left origin -- same canvas-relative meaning as bk_gfx_set_scissor. A width or
/// height <= 0 means "full target", which is also the default.
void bk_gfx_set_viewport(BK_Rect rect);

/// Renders this frame into canvas instead of the swapchain, then blits the canvas
/// onto the swapchain (stretched to fit, filtered per the canvas's blit_filter) once
/// the render pass ends -- a canvas smaller than the window is the fixed-internal-
/// resolution / pixel-art path. The binding is consumed (cleared) by the frame's
/// flush; pass nullptr (or don't call this) to render into the swapchain directly, as
/// before. A pipeline bound this frame must have been created with
/// BK_GfxPipelineDesc.color_target_format matching canvas's colour attachment (always
/// SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM) and .depth_stencil_format matching canvas's
/// depth attachment (or SDL_GPU_TEXTUREFORMAT_INVALID if canvas has none) --
/// BK_ASSERTs otherwise.
void bk_gfx_bind_canvas(BK_GfxCanvas *canvas);

/// Requests that the frame currently being rendered be saved as a BMP to path once
/// presented. path is copied internally (safe to pass a stack buffer built fresh each
/// frame) -- the request is consumed after the frame it applies to, so call again
/// each frame you want captured. Failures (bad path, unsupported swapchain
/// composition) are logged via SDL_Log with a "BK: " prefix, not returned -- the
/// actual capture happens later, inside the frame's flush. If bk_gfx_bind_canvas was
/// also called this frame, the capture happens after the canvas is blitted onto the
/// swapchain, so it captures the canvas's contents, not a blank clear.
void bk_gfx_request_capture(const char *path);
