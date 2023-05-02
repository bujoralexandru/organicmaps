#include "drape_frontend/car3d.hpp"

#include "drape_frontend/color_constants.hpp"
#include "drape_frontend/visual_params.hpp"

#include "shaders/program_manager.hpp"

#include "drape/texture_manager.hpp"

#include "indexer/map_style_reader.hpp"
#include "indexer/scales.hpp"

#include "geometry/screenbase.hpp"

namespace df
{
namespace car3d
{
double constexpr kArrowSize = 12.0;
double constexpr kCar3DScaleMin = 1.0;
double constexpr kCar3DScaleMax = 2.2;
int constexpr kCar3DMinZoom = 16;
}  // namespace car3d

float constexpr kOutlineScale = 1.2f;

// int constexpr kComponentsInVertex = 4;
// int constexpr kComponentsInNormal = 3;

// df::ColorConstant const kArrow3DShadowColor = "Arrow3DShadow";
// df::ColorConstant const kArrow3DObsoleteColor = "Arrow3DObsolete";
// df::ColorConstant const kArrow3DColor = "Arrow3D";
// df::ColorConstant const kArrow3DOutlineColor = "Arrow3DOutline";

Car3D::Car3D(ref_ptr<dp::GraphicsContext> context)
  : m_carMesh(context, dp::MeshObject::DrawPrimitive::Triangles)
  , m_state(CreateRenderState(gpu::Program::Car3D, DepthLayer::OverlayLayer))
{
  m_state.SetDepthTestEnabled(false);

  //  std::vector<float> normals =
  //      dp::MeshObject::GenerateNormalsForTriangles(vertices, kComponentsInVertex);
  //
  //  auto constexpr kVerticesBufferInd = 0;
  //  auto copiedVertices = vertices;
  //  m_carMesh.SetBuffer(kVerticesBufferInd, std::move(copiedVertices),
  //                        sizeof(float) * kComponentsInVertex);
  //  m_carMesh.SetAttribute("a_pos", kVerticesBufferInd, 0 /* offset */, kComponentsInVertex);
  //
  //  auto constexpr kNormalsBufferInd = 1;
  //  m_arrowMesh.SetBuffer(kNormalsBufferInd, std::move(normals), sizeof(float) *
  //  kComponentsInNormal); m_arrowMesh.SetAttribute("a_normal", kNormalsBufferInd, 0 /* offset */,
  //  kComponentsInNormal);
  //
  //  m_shadowMesh.SetBuffer(kVerticesBufferInd, std::move(vertices),
  //                         sizeof(float) * kComponentsInVertex);
  //  m_shadowMesh.SetAttribute("a_pos", kVerticesBufferInd, 0 /* offset */, kComponentsInVertex);
}

// static
double Car3D::GetMaxBottomSize()
{
  double const kBottomSize = 1.0;
  return kBottomSize * car3d::kArrowSize * car3d::kCar3DScaleMax * kOutlineScale;
}

void Car3D::SetPosition(const m2::PointD & position) { m_position = position; }

void Car3D::SetAzimuth(double azimuth) { m_azimuth = azimuth; }

void Car3D::SetTexture(ref_ptr<dp::TextureManager> texMng)
{
  m_state.SetColorTexture(texMng->GetSymbolsTexture());
}

void Car3D::SetPositionObsolete(bool obsolete) { m_obsoletePosition = obsolete; }

void Car3D::Render(ref_ptr<dp::GraphicsContext> context, ref_ptr<gpu::ProgramManager> mng,
                   ScreenBase const & screen, bool routingMode)
{
  // Render shadow.
  //  if (screen.isPerspective())
  //  {
  //    RenderCar(context, mng, m_shadowMesh, screen, gpu::Program::Car3DShadow,
  //                df::GetColorConstant(df::kArrow3DShadowColor), 0.05f /* dz */,
  //                routingMode ? kOutlineScale : 1.0f /* scaleFactor */);
  //  }

  // Render outline.
  //  if (routingMode)
  //  {
  //    dp::Color const outlineColor = df::GetColorConstant(df::kArrow3DOutlineColor);
  //    RenderArrow(context, mng, m_shadowMesh, screen, gpu::Program::Car3DOutline, outlineColor,
  //                0.0f /* dz */, kOutlineScale /* scaleFactor */);
  //  }

  // Render arrow.
  //  dp::Color const color =
  //      df::GetColorConstant(m_obsoletePosition ? df::kArrow3DObsoleteColor : df::kArrow3DColor);
  RenderCar(context, mng, m_carMesh, screen, gpu::Program::Car3D, 0.0f /* dz */,
            1.0f /* scaleFactor */);
}

void Car3D::RenderCar(ref_ptr<dp::GraphicsContext> context, ref_ptr<gpu::ProgramManager> mng,
                      dp::MeshObject & mesh, ScreenBase const & screen, gpu::Program program,
                      float dz, float scaleFactor)
{
  gpu::Car3dProgramParams params;
  math::Matrix<float, 4, 4> const modelTransform =
      CalculateTransform(screen, dz, scaleFactor, context->GetApiVersion());
  params.m_transform = glsl::make_mat4(modelTransform.m_data);

  auto gpuProgram = mng->GetProgram(program);
  mesh.Render(context, gpuProgram, m_state, mng->GetParamsSetter(), params);
}

math::Matrix<float, 4, 4> Car3D::CalculateTransform(ScreenBase const & screen, float dz,
                                                    float scaleFactor,
                                                    dp::ApiVersion apiVersion) const
{
  double arrowScale = VisualParams::Instance().GetVisualScale() * car3d::kArrowSize * scaleFactor;
  if (screen.isPerspective())
  {
    double const t = GetNormalizedZoomLevel(screen.GetScale(), car3d::kCar3DMinZoom);
    arrowScale *= (car3d::kCar3DScaleMin * (1.0 - t) + car3d::kCar3DScaleMax * t);
  }

  auto const scaleX = static_cast<float>(arrowScale * 2.0 / screen.PixelRect().SizeX());
  auto const scaleY = static_cast<float>(arrowScale * 2.0 / screen.PixelRect().SizeY());
  auto const scaleZ =
      static_cast<float>(screen.isPerspective() ? (0.002 * screen.GetDepth3d()) : 1.0);

  m2::PointD const pos = screen.GtoP(m_position);
  auto const dX = static_cast<float>(2.0 * pos.x / screen.PixelRect().SizeX() - 1.0);
  auto const dY = static_cast<float>(2.0 * pos.y / screen.PixelRect().SizeY() - 1.0);

  math::Matrix<float, 4, 4> scaleM = math::Identity<float, 4>();
  scaleM(0, 0) = scaleX;
  scaleM(1, 1) = scaleY;
  scaleM(2, 2) = scaleZ;

  math::Matrix<float, 4, 4> rotateM = math::Identity<float, 4>();
  rotateM(0, 0) = static_cast<float>(cos(m_azimuth + screen.GetAngle()));
  rotateM(0, 1) = static_cast<float>(-sin(m_azimuth + screen.GetAngle()));
  rotateM(1, 0) = -rotateM(0, 1);
  rotateM(1, 1) = rotateM(0, 0);

  math::Matrix<float, 4, 4> translateM = math::Identity<float, 4>();
  translateM(3, 0) = dX;
  translateM(3, 1) = -dY;
  translateM(3, 2) = dz;

  math::Matrix<float, 4, 4> modelTransform = rotateM * scaleM * translateM;
  if (screen.isPerspective())
    return modelTransform * math::Matrix<float, 4, 4>(screen.Pto3dMatrix());

  if (apiVersion == dp::ApiVersion::Metal)
  {
    modelTransform(3, 2) = modelTransform(3, 2) + 0.5f;
    modelTransform(2, 2) = modelTransform(2, 2) * 0.5f;
  }

  return modelTransform;
}
}  // namespace df
