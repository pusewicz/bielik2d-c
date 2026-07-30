#include "internal/bk_app_internal.h"
#include "internal/bk_gfx_pipeline_internal.h"
#include <SDL3/SDL.h>
#include <bielik/bk_app.h>
#include <bielik/bk_gfx_pipeline.h>

struct BK_GfxPipeline {
    SDL_GPUDevice *device;
    SDL_GPUGraphicsPipeline *handle;
};

static bool s_pick_shader_variant(SDL_GPUShaderFormat supported, const BK_GfxShaderDesc *desc,
                                  const void **out_code, size_t *out_code_size,
                                  const char **out_entry_point, SDL_GPUShaderFormat *out_format) {
    if ((supported & SDL_GPU_SHADERFORMAT_SPIRV) && desc->spirv.code != nullptr) {
        *out_code = desc->spirv.code;
        *out_code_size = desc->spirv.code_size;
        *out_entry_point = desc->spirv.entry_point;
        *out_format = SDL_GPU_SHADERFORMAT_SPIRV;
        return true;
    }
    if ((supported & SDL_GPU_SHADERFORMAT_DXIL) && desc->dxil.code != nullptr) {
        *out_code = desc->dxil.code;
        *out_code_size = desc->dxil.code_size;
        *out_entry_point = desc->dxil.entry_point;
        *out_format = SDL_GPU_SHADERFORMAT_DXIL;
        return true;
    }
    if ((supported & SDL_GPU_SHADERFORMAT_MSL) && desc->msl.code != nullptr) {
        *out_code = desc->msl.code;
        *out_code_size = desc->msl.code_size;
        *out_entry_point = desc->msl.entry_point;
        *out_format = SDL_GPU_SHADERFORMAT_MSL;
        return true;
    }
    return false;
}

static SDL_GPUShader *s_create_shader(SDL_GPUDevice *device, const BK_GfxShaderDesc *desc,
                                      SDL_GPUShaderStage stage) {
    const void *code = nullptr;
    size_t code_size = 0;
    const char *entry_point = nullptr;
    SDL_GPUShaderFormat format = SDL_GPU_SHADERFORMAT_INVALID;

    if (!s_pick_shader_variant(SDL_GetGPUShaderFormats(device), desc, &code, &code_size,
                               &entry_point, &format)) {
        SDL_Log("BK: bk_gfx_pipeline_create: no shader variant matches the device's supported "
                "formats");
        return nullptr;
    }

    SDL_GPUShaderCreateInfo info = {
        .code_size = code_size,
        .code = code,
        .entrypoint = entry_point,
        .format = format,
        .stage = stage,
        .num_samplers = (Uint32)desc->num_samplers,
        .num_uniform_buffers = (Uint32)desc->num_uniform_buffers,
    };
    SDL_GPUShader *shader = SDL_CreateGPUShader(device, &info);
    if (shader == nullptr) {
        SDL_Log("BK: SDL_CreateGPUShader failed: %s", SDL_GetError());
    }
    return shader;
}

static SDL_GPUVertexElementFormat s_vertex_format(BK_GfxVertexFormat format) {
    switch (format) {
    case BK_GFX_VERTEX_FORMAT_FLOAT2:
        return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    case BK_GFX_VERTEX_FORMAT_FLOAT3:
        return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    case BK_GFX_VERTEX_FORMAT_FLOAT4:
        return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
    case BK_GFX_VERTEX_FORMAT_UBYTE4_NORM:
        return SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM;
    }
    BK_ASSERT(false);
    return SDL_GPU_VERTEXELEMENTFORMAT_INVALID;
}

static SDL_GPUPrimitiveType s_primitive_type(BK_GfxPrimitiveType type) {
    switch (type) {
    case BK_GFX_PRIMITIVE_TRIANGLE_LIST:
        return SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    case BK_GFX_PRIMITIVE_TRIANGLE_STRIP:
        return SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP;
    case BK_GFX_PRIMITIVE_LINE_LIST:
        return SDL_GPU_PRIMITIVETYPE_LINELIST;
    }
    BK_ASSERT(false);
    return SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
}

BK_GfxPipeline *bk_gfx_pipeline_create(SDL_GPUDevice *device, const BK_GfxPipelineDesc *desc) {
    BK_ASSERT(device != nullptr);
    BK_ASSERT(desc != nullptr);

    if (desc->num_vertex_buffers < 0 || desc->num_vertex_buffers > 8) {
        SDL_Log("BK: bk_gfx_pipeline_create: num_vertex_buffers must be in [0, 8], got %d",
                desc->num_vertex_buffers);
        return nullptr;
    }
    if (desc->num_vertex_attributes < 0 || desc->num_vertex_attributes > 16) {
        SDL_Log("BK: bk_gfx_pipeline_create: num_vertex_attributes must be in [0, 16], got %d",
                desc->num_vertex_attributes);
        return nullptr;
    }

    SDL_GPUShader *vertex_shader =
        s_create_shader(device, &desc->vertex_shader, SDL_GPU_SHADERSTAGE_VERTEX);
    if (vertex_shader == nullptr) {
        return nullptr;
    }
    SDL_GPUShader *fragment_shader =
        s_create_shader(device, &desc->fragment_shader, SDL_GPU_SHADERSTAGE_FRAGMENT);
    if (fragment_shader == nullptr) {
        SDL_ReleaseGPUShader(device, vertex_shader);
        return nullptr;
    }

    SDL_GPUVertexBufferDescription sdl_buffers[8] = {0};
    for (int i = 0; i < desc->num_vertex_buffers; i++) {
        sdl_buffers[i] = (SDL_GPUVertexBufferDescription){
            .slot = desc->vertex_buffers[i].slot,
            .pitch = desc->vertex_buffers[i].pitch,
            .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
        };
    }

    SDL_GPUVertexAttribute sdl_attrs[16] = {0};
    for (int i = 0; i < desc->num_vertex_attributes; i++) {
        sdl_attrs[i] = (SDL_GPUVertexAttribute){
            .location = desc->vertex_attributes[i].location,
            .buffer_slot = desc->vertex_attributes[i].buffer_slot,
            .format = s_vertex_format(desc->vertex_attributes[i].format),
            .offset = desc->vertex_attributes[i].offset,
        };
    }

    SDL_GPUColorTargetBlendState blend = {0};
    if (desc->blend_mode == BK_GFX_BLEND_ALPHA) {
        blend = (SDL_GPUColorTargetBlendState){
            .src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
            .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            .color_blend_op = SDL_GPU_BLENDOP_ADD,
            .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
            .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            .alpha_blend_op = SDL_GPU_BLENDOP_ADD,
            .enable_blend = true,
        };
    }

    SDL_GPUColorTargetDescription color_target = {
        .format = desc->color_target_format,
        .blend_state = blend,
    };

    SDL_GPUGraphicsPipelineCreateInfo info = {
        .vertex_shader = vertex_shader,
        .fragment_shader = fragment_shader,
        .vertex_input_state =
            {
                .vertex_buffer_descriptions = sdl_buffers,
                .num_vertex_buffers = (Uint32)desc->num_vertex_buffers,
                .vertex_attributes = sdl_attrs,
                .num_vertex_attributes = (Uint32)desc->num_vertex_attributes,
            },
        .primitive_type = s_primitive_type(desc->primitive_type),
        .target_info =
            {
                .color_target_descriptions = &color_target,
                .num_color_targets = 1,
            },
    };

    SDL_GPUGraphicsPipeline *handle = SDL_CreateGPUGraphicsPipeline(device, &info);

    SDL_ReleaseGPUShader(device, vertex_shader);
    SDL_ReleaseGPUShader(device, fragment_shader);

    if (handle == nullptr) {
        SDL_Log("BK: SDL_CreateGPUGraphicsPipeline failed: %s", SDL_GetError());
        return nullptr;
    }

    BK_GfxPipeline *pipeline = bk__alloc(sizeof(BK_GfxPipeline));
    if (pipeline == nullptr) {
        SDL_ReleaseGPUGraphicsPipeline(device, handle);
        return nullptr;
    }
    pipeline->device = device;
    pipeline->handle = handle;
    return pipeline;
}

void bk_gfx_pipeline_destroy(BK_GfxPipeline *pipeline) {
    if (pipeline == nullptr) {
        return;
    }
    SDL_ReleaseGPUGraphicsPipeline(pipeline->device, pipeline->handle);
    bk__free(pipeline);
}

SDL_GPUGraphicsPipeline *bk__gfx_pipeline_handle(const BK_GfxPipeline *pipeline) {
    BK_ASSERT(pipeline != nullptr);
    return pipeline->handle;
}
