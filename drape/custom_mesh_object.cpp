#include "drape/custom_mesh_object.hpp"

#include "drape/gl_constants.hpp"
#include "drape/gl_gpu_program.hpp"
#include "drape/gl_functions.hpp"
#include "drape/glsl_func.hpp"
#include "drape/glsl_types.hpp"
#include "drape/texture_manager.hpp"

#include "base/assert.hpp"
#include <android/log.h>

namespace
{
glConst GetGLDrawPrimitive(dp::CustomMeshObject::DrawPrimitive drawPrimitive)
{
  switch (drawPrimitive)
  {
  case dp::CustomMeshObject::DrawPrimitive::Triangles: return gl_const::GLTriangles;
  case dp::CustomMeshObject::DrawPrimitive::TriangleStrip: return gl_const::GLTriangleStrip;
  case dp::CustomMeshObject::DrawPrimitive::LineStrip: return gl_const::GLLineStrip;
  }
  UNREACHABLE();
}
}  // namespace

namespace dp
{
class CustomGLMeshObjectImpl : public CustomMeshObjectImpl
{
public:
  explicit CustomGLMeshObjectImpl(ref_ptr<dp::CustomMeshObject> mesh)
    : m_mesh(mesh)
  {}

  void Build(ref_ptr<dp::GraphicsContext> context, ref_ptr<dp::GpuProgram> program) override
  {
    UNUSED_VALUE(context);

    bool const isVAOSupported =
      GLFunctions::ExtensionsList.IsSupported(dp::GLExtensionsList::VertexArrayObject);
    if (isVAOSupported)
    {
      m_VAO = GLFunctions::glGenVertexArray();
      GLFunctions::glBindVertexArray(m_VAO);
    }

    for (auto & buffer : m_mesh->m_buffers)
    {
      buffer.m_bufferId = GLFunctions::glGenBuffer();
      GLFunctions::glBindBuffer(buffer.m_bufferId, gl_const::GLArrayBuffer);

      if (!buffer.m_data.empty())
      {
        GLFunctions::glBufferData(gl_const::GLArrayBuffer,
                                  static_cast<uint32_t>(buffer.m_data.size()) * sizeof(buffer.m_data[0]),
                                  buffer.m_data.data(), gl_const::GLStaticDraw);
      }

      if (isVAOSupported)
      {
        ref_ptr<dp::GLGpuProgram> p = program;
        for (auto const & attribute : buffer.m_attributes)
        {
          int8_t const attributePosition = p->GetAttributeLocation(attribute.m_attributeName);
          ASSERT_NOT_EQUAL(attributePosition, -1, ());
          GLFunctions::glEnableVertexAttribute(attributePosition);
          GLFunctions::glVertexAttributePointer(attributePosition, attribute.m_componentsCount,
                                                gl_const::GLFloatType, false,
                                                buffer.m_stride, attribute.m_offset);
        }
      }
    }

    if (isVAOSupported)
      GLFunctions::glBindVertexArray(0);
    GLFunctions::glBindBuffer(0, gl_const::GLArrayBuffer);
  }

  void Reset() override
  {
    for (auto & buffer : m_mesh->m_buffers)
    {
      if (buffer.m_bufferId != 0)
      {
        GLFunctions::glDeleteBuffer(buffer.m_bufferId);
        buffer.m_bufferId = 0;
      }
    }

    if (m_VAO != 0)
      GLFunctions::glDeleteVertexArray(m_VAO);

    m_VAO = 0;
  }

  void UpdateBuffer(ref_ptr<dp::GraphicsContext> context, uint32_t bufferInd) override
  {
    UNUSED_VALUE(context);
    auto & buffer = m_mesh->m_buffers[bufferInd];
    GLFunctions::glBindBuffer(buffer.m_bufferId, gl_const::GLArrayBuffer);
    GLFunctions::glBufferData(gl_const::GLArrayBuffer,
                              static_cast<uint32_t>(buffer.m_data.size()) * sizeof(buffer.m_data[0]),
                              buffer.m_data.data(), gl_const::GLStaticDraw);
    GLFunctions::glBindBuffer(0, gl_const::GLArrayBuffer);
  }

  void Bind(ref_ptr<dp::GpuProgram> program) override
  {
    if (GLFunctions::ExtensionsList.IsSupported(dp::GLExtensionsList::VertexArrayObject))
    {
      GLFunctions::glBindVertexArray(m_VAO);
      return;
    }

    ref_ptr<dp::GLGpuProgram> p = program;
    for (auto const & buffer : m_mesh->m_buffers)
    {
      GLFunctions::glBindBuffer(buffer.m_bufferId, gl_const::GLArrayBuffer);
      for (auto const & attribute : buffer.m_attributes)
      {
        int8_t const attributePosition = p->GetAttributeLocation(attribute.m_attributeName);
        ASSERT_NOT_EQUAL(attributePosition, -1, ());
        GLFunctions::glEnableVertexAttribute(attributePosition);
        GLFunctions::glVertexAttributePointer(attributePosition, attribute.m_componentsCount,
                                              gl_const::GLFloatType, false,
                                              buffer.m_stride, attribute.m_offset);
      }
    }
  }

  void Unbind() override
  {
    if (GLFunctions::ExtensionsList.IsSupported(dp::GLExtensionsList::VertexArrayObject))
      GLFunctions::glBindVertexArray(0);
    GLFunctions::glBindBuffer(0, gl_const::GLArrayBuffer);
  }

  void DrawPrimitives(ref_ptr<dp::GraphicsContext> context, uint32_t verticesCount) override
  {
    UNUSED_VALUE(context);

    __android_log_print(ANDROID_LOG_INFO, "TinyObj", "# verticesCount   = %d", verticesCount);

    GLFunctions::glDrawArrays(GetGLDrawPrimitive(m_mesh->m_drawPrimitive), 0, verticesCount);
//    GLFunctions::glDrawArrays(GetGLDrawPrimitive(m_mesh->m_drawPrimitive), 0, 3 * verticesCount);
  }

private:
  ref_ptr<dp::CustomMeshObject> m_mesh;
  uint32_t m_VAO = 0;
};

CustomMeshObject::CustomMeshObject(ref_ptr<dp::GraphicsContext> context, DrawPrimitive drawPrimitive)
  : m_drawPrimitive(drawPrimitive)
{
  auto const apiVersion = context->GetApiVersion();
  if (apiVersion == dp::ApiVersion::OpenGLES2 || apiVersion == dp::ApiVersion::OpenGLES3)
  {
    __android_log_print(ANDROID_LOG_INFO, "TinyObj", "Loading for OpenGL");
    InitForOpenGL();
  }
  else if (apiVersion == dp::ApiVersion::Metal)
  {
#if defined(OMIM_METAL_AVAILABLE)
    InitForMetal();
#endif
  }
  else if (apiVersion == dp::ApiVersion::Vulkan)
  {
    InitForVulkan(context);
  }
  CHECK(m_impl != nullptr, ());
}

CustomMeshObject::~CustomMeshObject()
{
  Reset();
}

void CustomMeshObject::InitForOpenGL()
{
  m_impl = make_unique_dp<CustomGLMeshObjectImpl>(make_ref(this));
}

void CustomMeshObject::SetBuffer(uint32_t bufferInd, std::vector<float> && vertices, uint32_t stride)
{
  CHECK_LESS_OR_EQUAL(bufferInd, GetNextBufferIndex(), ());

  if (bufferInd == GetNextBufferIndex())
    m_buffers.emplace_back(std::move(vertices), stride);
  else
    m_buffers[bufferInd] = VertexBuffer(std::move(vertices), stride);

  Reset();
}

void CustomMeshObject::SetTexture(uint32_t textureInd, std::string texture)
{
    CHECK_LESS_OR_EQUAL(textureInd, GetNextTextureIndex(), ());

    if (textureInd == GetNextTextureIndex())
        m_textures.emplace_back(std::move(texture));
    else
        m_textures[textureInd] = std::move(texture);

    Reset();
}

void CustomMeshObject::SetAttribute(std::string const & attributeName, uint32_t bufferInd, uint32_t offset,
                              uint32_t componentsCount)
{
  CHECK_LESS(bufferInd, m_buffers.size(), ());
  m_buffers[bufferInd].m_attributes.emplace_back(attributeName, offset, componentsCount);

  Reset();
}

void CustomMeshObject::Reset()
{
  CHECK(m_impl != nullptr, ());
  m_impl->Reset();

  m_initialized = false;
}

void CustomMeshObject::UpdateBuffer(ref_ptr<dp::GraphicsContext> context, uint32_t bufferInd,
                              std::vector<float> && vertices)
{
  CHECK(m_initialized, ());
  CHECK_LESS(bufferInd, static_cast<uint32_t>(m_buffers.size()), ());
  CHECK(!vertices.empty(), ());

  auto & buffer = m_buffers[bufferInd];
  buffer.m_data = std::move(vertices);

  CHECK(m_impl != nullptr, ());
  m_impl->UpdateBuffer(context, bufferInd);
}

void CustomMeshObject::Build(ref_ptr<dp::GraphicsContext> context, ref_ptr<dp::GpuProgram> program)
{
  Reset();

  CHECK(m_impl != nullptr, ());
  m_impl->Build(context, program);

  m_initialized = true;
}

void CustomMeshObject::Bind(ref_ptr<dp::GraphicsContext> context, ref_ptr<dp::GpuProgram> program)
{
  program->Bind();

  if (!m_initialized)
    Build(context, program);

  CHECK(m_impl != nullptr, ());
  m_impl->Bind(program);
}

void CustomMeshObject::DrawPrimitives(ref_ptr<dp::GraphicsContext> context)
{
  if (m_buffers.empty())
    return;

  auto const & buffer = m_buffers[0];
//#ifdef DEBUG
//  for (size_t i = 1; i < m_buffers.size(); i++)
//  {
//    ASSERT_EQUAL(m_buffers[i].m_data.size() / m_buffers[i].m_stride,
//                 buffer.m_data.size() / buffer.m_stride, ());
//  }
//#endif
  auto const verticesCount =
      static_cast<uint32_t>(buffer.m_data.size() * sizeof(buffer.m_data[0]) / buffer.m_stride);

  CHECK(m_impl != nullptr, ());
  m_impl->DrawPrimitives(context, verticesCount);
}

void CustomMeshObject::Unbind(ref_ptr<dp::GpuProgram> program)
{
  program->Unbind();

  CHECK(m_impl != nullptr, ());
  m_impl->Unbind();
}

// static
std::vector<float> CustomMeshObject::GenerateNormalsForTriangles(std::vector<float> const & vertices,
                                                           size_t componentsCount)
{
  auto const trianglesCount = vertices.size() / (3 * componentsCount);
  std::vector<float> normals;
  normals.reserve(trianglesCount * 9);
  for (size_t triangle = 0; triangle < trianglesCount; ++triangle)
  {
    glsl::vec3 v[3];
    for (size_t vertex = 0; vertex < 3; ++vertex)
    {
      size_t const offset = triangle * componentsCount * 3 + vertex * componentsCount;
      v[vertex] = glsl::vec3(vertices[offset], vertices[offset + 1], vertices[offset + 2]);
    }

    glsl::vec3 normal = glsl::cross(glsl::vec3(v[1].x - v[0].x, v[1].y - v[0].y, v[1].z - v[0].z),
                                    glsl::vec3(v[2].x - v[0].x, v[2].y - v[0].y, v[2].z - v[0].z));
    normal = glsl::normalize(normal);

    for (size_t vertex = 0; vertex < 3; ++vertex)
    {
      normals.push_back(normal.x);
      normals.push_back(normal.y);
      normals.push_back(normal.z);
    }
  }
  return normals;
}
}  // namespace dp

