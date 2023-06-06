#pragma once

#include "drape/drape_global.hpp"
#include "drape/graphics_context.hpp"
#include "drape/pointers.hpp"
#include "drape/render_state.hpp"

#include <functional>
#include <string>
#include <vector>

namespace dp
{
class GpuProgram;
class GraphicsContext;
class CustomMeshObjectImpl;

namespace metal
{
class CustomMetalMeshObjectImpl;
}  // namespace metal

namespace vulkan
{
class CustomVulkanMeshObjectImpl;
}  // namespace vulkan

// This class implements a simple mesh object which does not use an index buffer.
// Use this class only for simple geometry.
class CustomMeshObject
{
  friend class CustomMeshObjectImpl;
  friend class CustomGLMeshObjectImpl;
  friend class metal::CustomMetalMeshObjectImpl;
  friend class vulkan::CustomVulkanMeshObjectImpl;

public:
  enum class DrawPrimitive: uint8_t
  {
    Triangles,
    TriangleStrip,
    LineStrip
  };

  CustomMeshObject(ref_ptr<dp::GraphicsContext> context, DrawPrimitive drawPrimitive);
  virtual ~CustomMeshObject();

  void SetBuffer(uint32_t bufferInd, std::vector<float> && vertices, uint32_t stride);
  void SetAttribute(std::string const & attributeName, uint32_t bufferInd, uint32_t offset, uint32_t componentsCount);

  void UpdateBuffer(ref_ptr<dp::GraphicsContext> context, uint32_t bufferInd, std::vector<float> && vertices);

  template <typename TParamsSetter, typename TParams>
  void Render(ref_ptr<dp::GraphicsContext> context, ref_ptr<dp::GpuProgram> program,
              dp::RenderState const & state, ref_ptr<TParamsSetter> paramsSetter,
              TParams const & params)
  {
    Bind(context, program);

    ApplyState(context, program, state);
    paramsSetter->Apply(context, program, params);

    DrawPrimitives(context);

    Unbind(program);
  };

  uint32_t GetNextBufferIndex() const { return static_cast<uint32_t>(m_buffers.size()); }

  bool IsInitialized() const { return m_initialized; }
  void Build(ref_ptr<dp::GraphicsContext> context, ref_ptr<dp::GpuProgram> program);
  void Reset();

  static std::vector<float> GenerateNormalsForTriangles(std::vector<float> const & vertices, size_t componentsCount);

private:
  struct AttributeMapping
  {
    AttributeMapping() = default;

    AttributeMapping(std::string const & attributeName, uint32_t offset, uint32_t componentsCount)
      : m_offset(offset)
      , m_componentsCount(componentsCount)
      , m_attributeName(attributeName)
    {}

    uint32_t m_offset = 0;
    uint32_t m_componentsCount = 0;
    std::string m_attributeName;
  };

  struct VertexBuffer
  {
    VertexBuffer() = default;

    VertexBuffer(std::vector<float> && data, uint32_t stride)
      : m_data(std::move(data))
      , m_stride(stride)
    {}

    std::vector<float> m_data;
    uint32_t m_stride = 0;
    uint32_t m_bufferId = 0;

    std::vector<AttributeMapping> m_attributes;
  };

  void InitForOpenGL();
  void InitForVulkan(ref_ptr<dp::GraphicsContext> context);

#if defined(OMIM_METAL_AVAILABLE)
  // Definition of this method is in a .mm-file.
  void InitForMetal();
#endif

  void Bind(ref_ptr<dp::GraphicsContext> context, ref_ptr<dp::GpuProgram> program);
  void Unbind(ref_ptr<dp::GpuProgram> program);
  void DrawPrimitives(ref_ptr<dp::GraphicsContext> context);

  std::vector<VertexBuffer> m_buffers;
  DrawPrimitive m_drawPrimitive = DrawPrimitive::Triangles;

  drape_ptr<CustomMeshObjectImpl> m_impl;
  bool m_initialized = false;
};

class CustomMeshObjectImpl
{
public:
  virtual ~CustomMeshObjectImpl() = default;
  virtual void Build(ref_ptr<dp::GraphicsContext> context, ref_ptr<dp::GpuProgram> program) = 0;
  virtual void Reset() = 0;
  virtual void UpdateBuffer(ref_ptr<dp::GraphicsContext> context, uint32_t bufferInd) = 0;
  virtual void Bind(ref_ptr<dp::GpuProgram> program) = 0;
  virtual void Unbind() = 0;
  virtual void DrawPrimitives(ref_ptr<dp::GraphicsContext> context, uint32_t verticesCount) = 0;
};
}  // namespace dp
