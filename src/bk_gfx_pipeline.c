#include "internal/bk_app_internal.h"
#include "internal/bk_gfx_buffer_internal.h"
#include "internal/bk_gfx_pipeline_internal.h"
#include "internal/bk_gfx_texture_internal.h"

#include <bielik/bk_app.h>
#include <bielik/bk_gfx_pipeline.h>

#include <SDL3/SDL.h>

struct BK_GfxPipeline {
  SDL_GPUDevice *device;
  SDL_GPUGraphicsPipeline *handle;
  SDL_GPUTextureFormat depth_stencil_format;
};

// Picks the variant matching the device's supported shader formats, trying
// SPIR-V/DXIL/MSL in that order. Shared by graphics shader creation (BK_GfxShaderDesc's
// three variants) and compute pipeline creation (BK_GfxComputePipelineDesc's three
// variants) so format-selection logic exists in exactly one place.
static const BK_GfxShaderVariant *s_pick_shader_variant(SDL_GPUShaderFormat supported,
                                                        const BK_GfxShaderVariant *spirv,
                                                        const BK_GfxShaderVariant *dxil,
                                                        const BK_GfxShaderVariant *msl,
                                                        SDL_GPUShaderFormat *out_format) {
  if ((supported & SDL_GPU_SHADERFORMAT_SPIRV) && spirv->code != nullptr) {
    *out_format = SDL_GPU_SHADERFORMAT_SPIRV;
    return spirv;
  }
  if ((supported & SDL_GPU_SHADERFORMAT_DXIL) && dxil->code != nullptr) {
    *out_format = SDL_GPU_SHADERFORMAT_DXIL;
    return dxil;
  }
  if ((supported & SDL_GPU_SHADERFORMAT_MSL) && msl->code != nullptr) {
    *out_format = SDL_GPU_SHADERFORMAT_MSL;
    return msl;
  }
  return nullptr;
}

static SDL_GPUShader *s_create_shader(SDL_GPUDevice *device, const BK_GfxShaderDesc *desc,
                                      SDL_GPUShaderStage stage) {
  SDL_GPUShaderFormat format = SDL_GPU_SHADERFORMAT_INVALID;
  const BK_GfxShaderVariant *variant = s_pick_shader_variant(
      SDL_GetGPUShaderFormats(device), &desc->spirv, &desc->dxil, &desc->msl, &format);
  if (variant == nullptr) {
    SDL_Log("BK: bk_gfx_pipeline_create: no shader variant matches the device's supported "
            "formats");
    return nullptr;
  }

  SDL_GPUShaderCreateInfo info = {
      .code_size = variant->code_size,
      .code = variant->code,
      .entrypoint = variant->entry_point,
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

static SDL_GPUCompareOp s_compare_op(BK_GfxCompare compare) {
  switch (compare) {
  case BK_GFX_COMPARE_ALWAYS:
    return SDL_GPU_COMPAREOP_ALWAYS;
  case BK_GFX_COMPARE_NEVER:
    return SDL_GPU_COMPAREOP_NEVER;
  case BK_GFX_COMPARE_LESS:
    return SDL_GPU_COMPAREOP_LESS;
  case BK_GFX_COMPARE_LESS_EQUAL:
    return SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
  case BK_GFX_COMPARE_GREATER:
    return SDL_GPU_COMPAREOP_GREATER;
  case BK_GFX_COMPARE_GREATER_EQUAL:
    return SDL_GPU_COMPAREOP_GREATER_OR_EQUAL;
  case BK_GFX_COMPARE_EQUAL:
    return SDL_GPU_COMPAREOP_EQUAL;
  case BK_GFX_COMPARE_NOT_EQUAL:
    return SDL_GPU_COMPAREOP_NOT_EQUAL;
  }
  BK_ASSERT(false);
  return SDL_GPU_COMPAREOP_ALWAYS;
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
                               .depth_stencil_format = desc->depth_stencil_format,
                               .has_depth_stencil_target =
                  desc->depth_stencil_format != SDL_GPU_TEXTUREFORMAT_INVALID,
                               },
  };

  // A pipeline can depth-test without depth-writing (e.g. an overlay that reads but
  // never occludes), so the test is enabled whenever either is requested -- not
  // gated on depth_write alone.
  bool has_depth = desc->depth_stencil_format != SDL_GPU_TEXTUREFORMAT_INVALID;
  info.depth_stencil_state = (SDL_GPUDepthStencilState){
      .compare_op = s_compare_op(desc->depth_compare),
      .enable_depth_test =
          has_depth && (desc->depth_write || desc->depth_compare != BK_GFX_COMPARE_ALWAYS),
      .enable_depth_write = has_depth && desc->depth_write,
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
  pipeline->depth_stencil_format = desc->depth_stencil_format;
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

SDL_GPUTextureFormat bk__gfx_pipeline_depth_format(const BK_GfxPipeline *pipeline) {
  BK_ASSERT(pipeline != nullptr);
  return pipeline->depth_stencil_format;
}

struct BK_GfxComputePipeline {
  SDL_GPUDevice *device;
  SDL_GPUComputePipeline *handle;
};

BK_GfxComputePipeline *bk_gfx_compute_pipeline_create(SDL_GPUDevice *device,
                                                      const BK_GfxComputePipelineDesc *desc) {
  BK_ASSERT(device != nullptr);
  BK_ASSERT(desc != nullptr);

  SDL_GPUShaderFormat format = SDL_GPU_SHADERFORMAT_INVALID;
  const BK_GfxShaderVariant *variant = s_pick_shader_variant(
      SDL_GetGPUShaderFormats(device), &desc->spirv, &desc->dxil, &desc->msl, &format);
  if (variant == nullptr) {
    SDL_Log("BK: bk_gfx_compute_pipeline_create: no shader variant matches the device's "
            "supported formats");
    return nullptr;
  }

  SDL_GPUComputePipelineCreateInfo info = {
      .code_size = variant->code_size,
      .code = variant->code,
      .entrypoint = variant->entry_point,
      .format = format,
      .num_readonly_storage_buffers = (Uint32)desc->num_readonly_storage_buffers,
      .num_readwrite_storage_textures = (Uint32)desc->num_readwrite_storage_textures,
      .threadcount_x = desc->threadcount_x,
      .threadcount_y = desc->threadcount_y,
      .threadcount_z = desc->threadcount_z,
  };
  SDL_GPUComputePipeline *handle = SDL_CreateGPUComputePipeline(device, &info);
  if (handle == nullptr) {
    SDL_Log("BK: SDL_CreateGPUComputePipeline failed: %s", SDL_GetError());
    return nullptr;
  }

  BK_GfxComputePipeline *pipeline = bk__alloc(sizeof(BK_GfxComputePipeline));
  if (pipeline == nullptr) {
    SDL_ReleaseGPUComputePipeline(device, handle);
    return nullptr;
  }
  pipeline->device = device;
  pipeline->handle = handle;
  return pipeline;
}

void bk_gfx_compute_pipeline_destroy(BK_GfxComputePipeline *pipeline) {
  if (pipeline == nullptr) {
    return;
  }
  SDL_ReleaseGPUComputePipeline(pipeline->device, pipeline->handle);
  bk__free(pipeline);
}

bool bk_gfx_compute_dispatch(const BK_GfxComputeDispatchDesc *desc) {
  BK_ASSERT(desc != nullptr);
  BK_ASSERT(desc->pipeline != nullptr);

  SDL_GPUDevice *device = desc->pipeline->device;

  SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(device);
  if (cmd == nullptr) {
    SDL_Log("BK: bk_gfx_compute_dispatch: SDL_AcquireGPUCommandBuffer failed: %s", SDL_GetError());
    return false;
  }

  SDL_GPUStorageTextureReadWriteBinding texture_bindings[8] = {0};
  BK_ASSERT(desc->num_readwrite_textures <= 8);
  for (int i = 0; i < desc->num_readwrite_textures; i++) {
    texture_bindings[i] = (SDL_GPUStorageTextureReadWriteBinding){
        .texture = bk__gfx_texture_handle(desc->readwrite_textures[i]),
    };
  }

  SDL_GPUComputePass *pass = SDL_BeginGPUComputePass(
      cmd, texture_bindings, (Uint32)desc->num_readwrite_textures, nullptr, 0);
  SDL_BindGPUComputePipeline(pass, desc->pipeline->handle);

  if (desc->num_readonly_buffers > 0) {
    SDL_GPUBuffer *storage_buffers[8];
    BK_ASSERT(desc->num_readonly_buffers <= 8);
    for (int i = 0; i < desc->num_readonly_buffers; i++) {
      storage_buffers[i] = bk__gfx_buffer_handle(desc->readonly_buffers[i]);
    }
    SDL_BindGPUComputeStorageBuffers(pass, 0, storage_buffers, (Uint32)desc->num_readonly_buffers);
  }

  SDL_DispatchGPUCompute(pass, desc->groups_x, desc->groups_y, desc->groups_z);
  SDL_EndGPUComputePass(pass);

  SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
  if (fence == nullptr) {
    SDL_Log("BK: bk_gfx_compute_dispatch: SDL_SubmitGPUCommandBufferAndAcquireFence failed: %s",
            SDL_GetError());
    return false;
  }
  if (!SDL_WaitForGPUFences(device, true, &fence, 1)) {
    SDL_Log("BK: bk_gfx_compute_dispatch: SDL_WaitForGPUFences failed: %s", SDL_GetError());
    SDL_ReleaseGPUFence(device, fence);
    return false;
  }
  SDL_ReleaseGPUFence(device, fence);
  return true;
}
