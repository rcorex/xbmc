/*
 *  Copyright (C) 2025 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "VideoBuffer.h"

#include <atomic>
#include <memory>
#include <mutex>

class AcbHandle
{
public:
  AcbHandle();
  ~AcbHandle() noexcept;

  AcbHandle(const AcbHandle&) = delete;
  AcbHandle& operator=(const AcbHandle&) = delete;

  [[nodiscard]] long Id() const noexcept;
  [[nodiscard]] long& TaskId() noexcept;

  void Reset() noexcept;

private:
  long m_handle{0};
  long m_taskId{0};
};

class CStarfishVideoBuffer final : public CVideoBuffer
{
public:
  explicit CStarfishVideoBuffer();
  AVPixelFormat GetFormat() override;
  // Thread-safe handle access to prevent use-after-free across threads
  std::shared_ptr<AcbHandle> GetAcbHandle() const;
  std::shared_ptr<AcbHandle> CreateAcbHandle();
  void ResetAcbHandle();
  void InvalidateDisplayWindow() noexcept { m_displayWindowGen.fetch_add(1, std::memory_order_relaxed); }
  [[nodiscard]] unsigned int DisplayWindowGen() const noexcept { return m_displayWindowGen.load(std::memory_order_relaxed); }

private:
  mutable std::mutex m_acbMutex;
  std::shared_ptr<AcbHandle> m_acbHandle{nullptr};
  std::atomic<unsigned int> m_displayWindowGen{1};
};
