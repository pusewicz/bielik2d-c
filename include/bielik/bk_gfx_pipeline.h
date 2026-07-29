#pragma once
#include <SDL3/SDL_gpu.h>
#include <stddef.h>
#include <stdint.h>

/// One precompiled shader bytecode blob for a single backend format.
typedef struct BK_GfxShaderVariant {
    const void *code;
    size_t code_size;
    const char *entry_point;
} BK_GfxShaderVariant;

/// One shader stage, precompiled to all three backend formats; bk_gfx_pipeline_create
/// selects the variant matching the device's supported shader formats
/// (SDL_GetGPUShaderFormats). Resource counts must match what the shader binary
/// declares (SDL_GPU validates this at creation). A variant with code == nullptr is
/// treated as unavailable.
typedef struct BK_GfxShaderDesc {
    BK_GfxShaderVariant spirv;
    BK_GfxShaderVariant dxil;
    BK_GfxShaderVariant msl;
    int num_samplers;
    int num_uniform_buffers;
} BK_GfxShaderDesc;

/// Per-vertex attribute formats supported by pipeline vertex input state.
typedef enum BK_GfxVertexFormat {
    BK_GFX_VERTEX_FORMAT_FLOAT2,
    BK_GFX_VERTEX_FORMAT_FLOAT3,
    BK_GFX_VERTEX_FORMAT_FLOAT4,
    BK_GFX_VERTEX_FORMAT_UBYTE4_NORM, // packed color
} BK_GfxVertexFormat;

/// One vertex attribute: which shader input location it feeds, which vertex buffer
/// slot it reads from, its format, and its byte offset within that slot's stride.
typedef struct BK_GfxVertexAttribute {
    uint32_t location;
    uint32_t buffer_slot;
    BK_GfxVertexFormat format;
    uint32_t offset;
} BK_GfxVertexAttribute;

/// One vertex buffer slot's stride, in bytes.
typedef struct BK_GfxVertexBufferLayout {
    uint32_t slot;
    uint32_t pitch;
} BK_GfxVertexBufferLayout;

typedef enum BK_GfxPrimitiveType {
    BK_GFX_PRIMITIVE_TRIANGLE_LIST,
    BK_GFX_PRIMITIVE_TRIANGLE_STRIP,
    BK_GFX_PRIMITIVE_LINE_LIST,
} BK_GfxPrimitiveType;

/// Fixed-function blend state. Two modes cover 2D's needs; more get added when a
/// real use case needs SDL_GPU's full blend-factor/op matrix.
typedef enum BK_GfxBlendMode {
    BK_GFX_BLEND_NONE,
    BK_GFX_BLEND_ALPHA,
} BK_GfxBlendMode;

/// Opaque graphics pipeline: compiled shaders + fixed-function state, bound in a
/// render pass before a draw call. Owns no per-frame resources.
typedef struct BK_GfxPipeline BK_GfxPipeline;

typedef struct BK_GfxPipelineDesc {
    BK_GfxShaderDesc vertex_shader;
    BK_GfxShaderDesc fragment_shader;

    // nullptr/0 => no vertex input (e.g. a procedural triangle driven by
    // gl_VertexIndex/SV_VertexID with no bound vertex buffer). Max 8 buffers, 16
    // attributes.
    const BK_GfxVertexBufferLayout *vertex_buffers;
    int num_vertex_buffers;
    const BK_GfxVertexAttribute *vertex_attributes;
    int num_vertex_attributes;

    BK_GfxPrimitiveType primitive_type;

    // Caller supplies the target format explicitly -- SDL_GetGPUSwapchainTextureFormat
    // for on-screen rendering; an offscreen texture's own format for headless/canvas
    // use. No bk_ wrapper needed.
    SDL_GPUTextureFormat color_target_format;
    BK_GfxBlendMode blend_mode;
} BK_GfxPipelineDesc;

/// Creates a graphics pipeline against the given device. Logs via SDL_Log ("BK: "
/// prefix) and returns nullptr on any SDL_GPU failure (bad bytecode, unsupported
/// format/resource combination) -- this is a runtime-data-dependent operation, not a
/// programmer-error precondition, so failure is a recoverable return, not an assert.
/// device is explicit (not the bk_gpu() singleton) so pipelines can be created and
/// tested without a running app or window.
BK_GfxPipeline *bk_gfx_pipeline_create(SDL_GPUDevice *device, const BK_GfxPipelineDesc *desc);

/// Destroys a pipeline. No-op if pipeline is nullptr.
void bk_gfx_pipeline_destroy(BK_GfxPipeline *pipeline);
