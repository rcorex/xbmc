/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "RendererStarfish.h"

#include "../RenderFactory.h"
#include "ServiceBroker.h"
#include "rendering/gles/RenderSystemGLES.h"
#include "settings/MediaSettings.h"
#include "utils/log.h"
#include "windowing/wayland/WinSystemWaylandWebOS.h"
#include "system_gl.h"

#include <appswitching-control-block/AcbAPI.h>
#include <vector>

CRendererStarfish::CRendererStarfish()
{
  CLog::LogF(LOGINFO, "Instanced");
}

CRendererStarfish::~CRendererStarfish()
{
  CServiceBroker::GetWinSystem()->GetGfxContext().SetTransferPQ(false);
}

CBaseRenderer* CRendererStarfish::Create(CVideoBuffer* buffer)
{
  if (buffer && dynamic_cast<CStarfishVideoBuffer*>(buffer))
    return new CRendererStarfish();
  return nullptr;
}

bool CRendererStarfish::Configure(const VideoPicture& picture,
                                  float fps,
                                  const unsigned int orientation)
{
  m_videoBuffer = static_cast<CStarfishVideoBuffer*>(picture.videoBuffer);
  if (m_videoBuffer->GetAcbHandle())
  {
    EnableAlwaysClip();
  }
  m_iFlags = GetFlagsChromaPosition(picture.chroma_position) |
             GetFlagsColorMatrix(picture.color_space, picture.iWidth, picture.iHeight) |
             GetFlagsColorPrimaries(picture.color_primaries) |
             GetFlagsStereoMode(picture.stereoMode);

  m_format = picture.videoBuffer->GetFormat();
  m_sourceWidth = picture.iWidth;
  m_sourceHeight = picture.iHeight;
  m_renderOrientation = orientation;

  // Calculate the input frame aspect ratio.
  CalculateFrameAspectRatio(picture.iDisplayWidth, picture.iDisplayHeight);
  SetViewMode(m_videoSettings.m_ViewMode);
  ManageRenderArea();

  if (picture.color_transfer == AVCOL_TRC_SMPTE2084 ||
      picture.hdrType == StreamHdrType::HDR_TYPE_DOLBYVISION)
  {
    if (CServiceBroker::GetWinSystem()->IsHDRDisplay())
      CServiceBroker::GetWinSystem()->GetGfxContext().SetTransferPQ(true);
  }

  m_configured = true;

  return true;
}

bool CRendererStarfish::IsConfigured()
{
  return m_configured;
}

bool CRendererStarfish::ConfigChanged(const VideoPicture& picture)
{
  if (picture.videoBuffer->GetFormat() != m_format)
  {
    return true;
  }

  return false;
}

bool CRendererStarfish::Register()
{
  VIDEOPLAYER::CRendererFactory::RegisterRenderer("starfish", CRendererStarfish::Create);
  return true;
}

void CRendererStarfish::ManageRenderArea()
{
  // this hack is needed to get the 2D mode of a 3D movie going
  const RenderStereoMode stereoMode =
      CServiceBroker::GetWinSystem()->GetGfxContext().GetStereoMode();
  if (stereoMode == RenderStereoMode::MONO)
    CServiceBroker::GetWinSystem()->GetGfxContext().SetStereoView(RenderStereoView::LEFT);

  CBaseRenderer::ManageRenderArea();

  if (stereoMode == RenderStereoMode::MONO)
    CServiceBroker::GetWinSystem()->GetGfxContext().SetStereoView(RenderStereoView::OFF);

  switch (stereoMode)
  {
    case RenderStereoMode::SPLIT_HORIZONTAL:
      m_destRect.y2 *= 2.0f;
      break;
    case RenderStereoMode::SPLIT_VERTICAL:
      m_destRect.x2 *= 2.0f;
      break;
    default:
      break;
  }

  if ((m_exportedDestRect != m_destRect || m_exportedSourceRect != m_sourceRect) &&
      !m_sourceRect.IsEmpty() && !m_destRect.IsEmpty())
  {
    const auto origRect =
        CRect{0, 0, static_cast<float>(m_sourceWidth), static_cast<float>(m_sourceHeight)};
    using namespace KODI::WINDOWING::WAYLAND;
    auto winSystem = static_cast<CWinSystemWaylandWebOS*>(CServiceBroker::GetWinSystem());
    if (winSystem->SupportsExportedWindow())
    {
      winSystem->SetExportedWindow(origRect, m_sourceRect, m_destRect);
    }
    else if (m_videoBuffer->GetAcbHandle())
    {
      AcbAPI_setCustomDisplayWindow(
          m_videoBuffer->GetAcbHandle()->Id(), static_cast<long>(m_sourceRect.x1),
          static_cast<long>(m_sourceRect.y1), static_cast<long>(m_sourceRect.Width()),
          static_cast<long>(m_sourceRect.Height()), static_cast<long>(m_destRect.x1),
          static_cast<long>(m_destRect.y1), static_cast<long>(m_destRect.Width()),
          static_cast<long>(m_destRect.Height()), false, &m_videoBuffer->GetAcbHandle()->TaskId());
    }
    m_exportedSourceRect = m_sourceRect;
    m_exportedDestRect = m_destRect;
  }
}

bool CRendererStarfish::Supports(const ERENDERFEATURE feature) const
{
  return (feature == RENDERFEATURE_ZOOM || feature == RENDERFEATURE_STRETCH ||
          feature == RENDERFEATURE_PIXEL_RATIO || feature == RENDERFEATURE_VERTICAL_SHIFT ||
          feature == RENDERFEATURE_ROTATION);
}

bool CRendererStarfish::Supports(ESCALINGMETHOD method) const
{
  return false;
}

bool CRendererStarfish::SupportsMultiPassRendering()
{
  return false;
}

void CRendererStarfish::AddVideoPicture(const VideoPicture& picture, int index)
{
}

void CRendererStarfish::ReleaseBuffer(int idx)
{
}

CRenderInfo CRendererStarfish::GetRenderInfo()
{
  CRenderInfo info;
  info.max_buffer_size = 4;
  return info;
}

bool CRendererStarfish::IsGuiLayer()
{
  return false;
}

bool CRendererStarfish::RenderCapture(int index, CRenderCapture* capture)
{
  return false;
}

void CRendererStarfish::UnInit()
{
  m_configured = false;
  if (m_blackBarVBO)
  {
    glDeleteBuffers(1, &m_blackBarVBO);
    m_blackBarVBO = 0;
  }
}

void CRendererStarfish::Update()
{
}

void CRendererStarfish::DrawBlackBars()
{
  CRect windowRect(0, 0, CServiceBroker::GetWinSystem()->GetGfxContext().GetWidth(),
                   CServiceBroker::GetWinSystem()->GetGfxContext().GetHeight());

  struct Svertex
  {
    float x, y;
  };

  if (m_lastWindowRect != windowRect || m_lastDestRect != m_destRect)
  {
    m_lastWindowRect = windowRect;
    m_lastDestRect = m_destRect;

    std::vector<CRect> quads;

    // Top bar
    if (m_destRect.y1 > windowRect.y1)
      quads.emplace_back(windowRect.x1, windowRect.y1, windowRect.x2, m_destRect.y1);
    // Bottom bar
    if (m_destRect.y2 < windowRect.y2)
      quads.emplace_back(windowRect.x1, m_destRect.y2, windowRect.x2, windowRect.y2);
    // Left bar
    if (m_destRect.x1 > windowRect.x1)
      quads.emplace_back(windowRect.x1, m_destRect.y1, m_destRect.x1, m_destRect.y2);
    // Right bar
    if (m_destRect.x2 < windowRect.x2)
      quads.emplace_back(m_destRect.x2, m_destRect.y1, windowRect.x2, m_destRect.y2);

    std::vector<Svertex> vertices(6 * quads.size());
    m_blackBarVertexCount = vertices.size();

    size_t count = 0;
    for (const auto& quad : quads)
    {
      vertices[count + 1].x = quad.x1;
      vertices[count + 1].y = quad.y1;

      vertices[count + 0].x = vertices[count + 5].x = quad.x1;
      vertices[count + 0].y = vertices[count + 5].y = quad.y2;

      vertices[count + 2].x = vertices[count + 3].x = quad.x2;
      vertices[count + 2].y = vertices[count + 3].y = quad.y1;

      vertices[count + 4].x = quad.x2;
      vertices[count + 4].y = quad.y2;

      count += 6;
    }

    if (!m_blackBarVBO)
    {
      glGenBuffers(1, &m_blackBarVBO);
    }
    glBindBuffer(GL_ARRAY_BUFFER, m_blackBarVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(Svertex) * vertices.size(), vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
  }

  if (m_blackBarVertexCount == 0 || !m_blackBarVBO)
    return;

  CRenderSystemGLES* renderSystem =
      dynamic_cast<CRenderSystemGLES*>(CServiceBroker::GetRenderSystem());
  if (!renderSystem)
    return;

  glDisable(GL_BLEND);

  renderSystem->EnableGUIShader(ShaderMethodGLES::SM_DEFAULT);
  GLint posLoc = renderSystem->GUIShaderGetPos();
  GLint uniCol = renderSystem->GUIShaderGetUniCol();
  GLint depthLoc = renderSystem->GUIShaderGetDepth();

  glUniform4f(uniCol, 0.0f, 0.0f, 0.0f, 1.0f);
  glUniform1f(depthLoc, -1.0f);

  glBindBuffer(GL_ARRAY_BUFFER, m_blackBarVBO);
  glVertexAttribPointer(posLoc, 2, GL_FLOAT, GL_FALSE, sizeof(Svertex), 0);
  glEnableVertexAttribArray(posLoc);

  glDrawArrays(GL_TRIANGLES, 0, m_blackBarVertexCount);

  glDisableVertexAttribArray(posLoc);
  glBindBuffer(GL_ARRAY_BUFFER, 0);

  renderSystem->DisableGUIShader();

  glEnable(GL_BLEND);
}

void CRendererStarfish::RenderUpdate(
    int index, int index2, bool clear, unsigned int flags, unsigned int alpha)
{
  if (!m_configured)
  {
    return;
  }

  ManageRenderArea();

  if (clear)
  {
    DrawBlackBars();
  }
}
