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

/// Depth-test comparison function. ALWAYS is first (== 0) so a zero-initialized
/// BK_GfxPipelineDesc means "no depth test" -- the existing default for every
/// pipeline created before this enum existed.
typedef enum BK_GfxCompare {
    BK_GFX_COMPARE_ALWAYS,
    BK_GFX_COMPARE_NEVER,
    BK_GFX_COMPARE_LESS,
    BK_GFX_COMPARE_LESS_EQUAL,
    BK_GFX_COMPARE_GREATER,
    BK_GFX_COMPARE_GREATER_EQUAL,
    BK_GFX_COMPARE_EQUAL,
    BK_GFX_COMPARE_NOT_EQUAL,
} BK_GfxCompare;

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

    // SDL_GPU_TEXTUREFORMAT_INVALID (0) => the render pass this pipeline draws into
    // has no depth-stencil attachment. Set to bk_gfx_depth_stencil_format(device) (see
    // bk_gfx_canvas.h) to match a canvas's or the swapchain's depth texture -- SDL_GPU
    // requires a pipeline's declared depth_stencil_format to equal the render pass's
    // attachment format exactly, or pipeline binding fails validation.
    SDL_GPUTextureFormat depth_stencil_format;
    BK_GfxCompare depth_compare;
    bool depth_write;
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

typedef struct BK_GfxBuffer BK_GfxBuffer;
typedef struct BK_GfxTexture BK_GfxTexture;

/// One compute shader, precompiled to all three backend formats, plus the resource
/// counts and threadgroup size the shader binary declares. threadcount_x/y/z must
/// match the shader source's local_size_{x,y,z} -- SDL_GPU validates this at creation.
typedef struct BK_GfxComputePipelineDesc {
    BK_GfxShaderVariant spirv;
    BK_GfxShaderVariant dxil;
    BK_GfxShaderVariant msl;
    int num_readonly_storage_buffers;
    int num_readwrite_storage_textures;
    uint32_t threadcount_x;
    uint32_t threadcount_y;
    uint32_t threadcount_z;
} BK_GfxComputePipelineDesc;

/// Opaque compute pipeline: a compiled compute shader, bound and dispatched via
/// bk_gfx_compute_dispatch. Owns no per-dispatch resources.
typedef struct BK_GfxComputePipeline BK_GfxComputePipeline;

/// Creates a compute pipeline against the given device. Logs via SDL_Log ("BK: "
/// prefix) and returns nullptr on any SDL_GPU failure. device is explicit, same
/// rationale as bk_gfx_pipeline_create.
BK_GfxComputePipeline *bk_gfx_compute_pipeline_create(SDL_GPUDevice *device,
                                                      const BK_GfxComputePipelineDesc *desc);

/// Destroys a compute pipeline. No-op if pipeline is nullptr.
void bk_gfx_compute_pipeline_destroy(BK_GfxComputePipeline *pipeline);

/// Describes one dispatch: which pipeline, which resources are bound to it (array
/// order matches binding slot order), and the workgroup counts.
typedef struct BK_GfxComputeDispatchDesc {
    BK_GfxComputePipeline *pipeline;
    BK_GfxTexture *const *readwrite_textures;
    int num_readwrite_textures;
    BK_GfxBuffer *const *readonly_buffers;
    int num_readonly_buffers;
    uint32_t groups_x;
    uint32_t groups_y;
    uint32_t groups_z;
} BK_GfxComputeDispatchDesc;

/// Dispatches a compute pipeline synchronously: acquires its own command buffer,
/// records the dispatch, submits, and blocks on a GPU fence until it completes.
/// Intended for setup-time work (e.g. procedurally filling a texture once at init),
/// not a per-frame call -- unlike bk_gfx_draw, there is no pending-slot/flush
/// integration, since compute work has no natural once-per-frame cadence the way the
/// render pass does. Returns false and logs via SDL_Log on SDL_GPU failure.
bool bk_gfx_compute_dispatch(const BK_GfxComputeDispatchDesc *desc);
