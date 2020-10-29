// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromecast/media/cma/backend/desktop/video_window_desktop.h"
#include "chromecast/public/graphics_types.h"

namespace chromecast {
namespace media {

VideoWindowDesktop::VideoWindowDesktop() : z_order_(0) {}

VideoWindowDesktop::~VideoWindowDesktop() {}

void VideoWindowDesktop::SetGeometry(const RectF& screen_rect, Transform transform) {

  LOG(INFO) << __FUNCTION__ << " this=" <<this<<" screen_rect="
            << screen_rect.width << "x"
            << screen_rect.height << " @" << screen_rect.x << ","
            << screen_rect.y << " transform " << transform;
}

int VideoWindowDesktop::GetZOrder() {
  return z_order_;
}

void VideoWindowDesktop::SetZOrder(int z_order) {
  LOG(INFO) << __FUNCTION__ << " z_order="<<z_order_;
  z_order_ = z_order;
}

void VideoWindowDesktop::SetVideoMute(bool muted) {
  LOG(INFO) << __FUNCTION__ << " muted="<<muted;
}

}  // namespace media
}  // namespace chromecast
