// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMECAST_MEDIA_CMA_BACKEND_DESKTOP_VIDEO_WINDOW_DESKTOP_H_
#define CHROMECAST_MEDIA_CMA_BACKEND_DESKTOP_VIDEO_WINDOW_DESKTOP_H_

#include <memory>

#include "base/macros.h"
#include "base/time/time.h"
#include "chromecast/public/video_window.h"

namespace chromecast {
namespace media {

class VideoWindowDesktop : public VideoWindow {
 public:
  VideoWindowDesktop();
  ~VideoWindowDesktop() override;

  void SetGeometry(const RectF& screen_rect, Transform transform) override;

  int GetZOrder() override;

  void SetZOrder(int z_order) override;

  void SetVideoMute(bool muted) override;

 private:
  int z_order_;
  DISALLOW_COPY_AND_ASSIGN(VideoWindowDesktop);
};

}  // namespace media
}  // namespace chromecast

#endif  // CHROMECAST_MEDIA_CMA_BACKEND_DESKTOP_VIDEO_WINDOW_DESKTOP_H_
