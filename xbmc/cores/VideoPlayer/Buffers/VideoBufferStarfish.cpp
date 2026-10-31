/*
 *  Copyright (C) 2025 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "VideoBufferStarfish.h"

#include <appswitching-control-block/AcbAPI.h>

AcbHandle::AcbHandle() : m_handle(AcbAPI_create())
{
}

AcbHandle::~AcbHandle() noexcept
{
  Reset();
}

long AcbHandle::Id() const noexcept
{
  return m_handle;
}

long& AcbHandle::TaskId() noexcept
{
  return m_taskId;
}

void AcbHandle::Reset() noexcept
{
  if (m_handle)
  {
    AcbAPI_finalize(m_handle);
    AcbAPI_destroy(m_handle);
    m_handle = 0;
    m_taskId = 0;
  }
}

CStarfishVideoBuffer::CStarfishVideoBuffer() : CVideoBuffer(0)
{
}

AVPixelFormat CStarfishVideoBuffer::GetFormat()
{
  return AV_PIX_FMT_NONE;
}

std::shared_ptr<AcbHandle> CStarfishVideoBuffer::GetAcbHandle() const
{
  std::lock_guard<std::mutex> lock(m_acbMutex);
  return m_acbHandle;
}

std::shared_ptr<AcbHandle> CStarfishVideoBuffer::CreateAcbHandle()
{
  std::lock_guard<std::mutex> lock(m_acbMutex);
  m_acbHandle = std::make_shared<AcbHandle>();
  InvalidateDisplayWindow();
  return m_acbHandle;
}

void CStarfishVideoBuffer::ResetAcbHandle()
{
  std::shared_ptr<AcbHandle> oldHandle;
  {
    std::lock_guard<std::mutex> lock(m_acbMutex);
    oldHandle = std::move(m_acbHandle);
  }
  // Destroy old handle outside lock to avoid blocking during AcbAPI teardown
}
