// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromecast/media/base/video_plane_controller.h"

#include <stddef.h>
#include <stdint.h>

#include <vector>

#include "base/bind.h"
#include "base/location.h"
#include "base/macros.h"
#include "base/single_thread_task_runner.h"
#include "base/time/time.h"
#include "chromecast/public/cast_media_shlib.h"
#include "chromecast/media/base/video_window_controller.h"

namespace chromecast {
namespace media {

namespace {

bool RectFEqual(const RectF& r1, const RectF& r2) {
  return r1.x == r2.x && r1.y == r2.y && r1.width == r2.width &&
         r1.height == r2.height;
}

bool SizeEqual(const Size& s1, const Size& s2) {
  return s1.width == s2.width && s1.height == s2.height;
}

bool DisplayRectFValid(const RectF& r) {
  return r.width >= 0 && r.height >= 0;
}

bool ResolutionSizeValid(const Size& s) {
  return s.width >= 0 && s.height >= 0;
}

// Translates a gfx::OverlayTransform into a VideoPlane::Transform.
// Could be just a lookup table once we have unit tests for this code
// to ensure it stays in sync with OverlayTransform.
chromecast::media::VideoPlane::Transform ConvertTransform(
    gfx::OverlayTransform transform) {
  switch (transform) {
    case gfx::OVERLAY_TRANSFORM_NONE:
      return chromecast::media::VideoPlane::TRANSFORM_NONE;
    case gfx::OVERLAY_TRANSFORM_FLIP_HORIZONTAL:
      return chromecast::media::VideoPlane::FLIP_HORIZONTAL;
    case gfx::OVERLAY_TRANSFORM_FLIP_VERTICAL:
      return chromecast::media::VideoPlane::FLIP_VERTICAL;
    case gfx::OVERLAY_TRANSFORM_ROTATE_90:
      return chromecast::media::VideoPlane::ROTATE_90;
    case gfx::OVERLAY_TRANSFORM_ROTATE_180:
      return chromecast::media::VideoPlane::ROTATE_180;
    case gfx::OVERLAY_TRANSFORM_ROTATE_270:
      return chromecast::media::VideoPlane::ROTATE_270;
    default:
      NOTREACHED();
      return chromecast::media::VideoPlane::TRANSFORM_NONE;
  }
}

}  // namespace

VideoPlaneController::VideoPlaneController(
    const Size& graphics_resolution,
    scoped_refptr<base::SingleThreadTaskRunner> media_task_runner)
    : is_paused_(false),
      have_screen_res_(true),
      screen_res_(1920, 1080),
      graphics_plane_res_(graphics_resolution),
      have_video_plane_geometry_(false),
      video_plane_display_rect_(0, 0),
      video_plane_transform_(VideoPlane::TRANSFORM_NONE),
      media_task_runner_(media_task_runner) {}

VideoPlaneController::~VideoPlaneController() {}

void VideoPlaneController::SetGeometry(const gfx::RectF& gfx_display_rect,
                                       gfx::OverlayTransform gfx_transform) {
  const RectF display_rect(gfx_display_rect.x(), gfx_display_rect.y(),
                           gfx_display_rect.width(), gfx_display_rect.height());
  VideoPlane::Transform transform = ConvertTransform(gfx_transform);
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(DisplayRectFValid(display_rect));
  if (have_video_plane_geometry_ &&
      RectFEqual(display_rect, video_plane_display_rect_) &&
      transform == video_plane_transform_) {
    return;
  }

  LOG(ERROR) << " OOOPS. Call reached here somehow. "
          << "New geometry parameters "
          << " rect=" << display_rect.width << "x" << display_rect.height
          << " @" << display_rect.x << "," << display_rect.y << " transform "
          << transform;

  have_video_plane_geometry_ = true;
  video_plane_display_rect_ = display_rect;
  video_plane_transform_ = transform;
}

void VideoPlaneController::SetScreenResolution(const Size& resolution) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(ResolutionSizeValid(resolution));
  if (have_screen_res_ && SizeEqual(resolution, screen_res_)) {
    VLOG(2) << "No change found in screen resolution.";
    return;
  }

  LOG(INFO) << "Got New screen resolution " << resolution.width << "x"
             << resolution.height;

  have_screen_res_ = true;
  screen_res_ = resolution;
  for (auto it=video_window_map_.begin(); it!=video_window_map_.end(); ++it) {
    if(it->second) {
      it->second->SetScreenResolution(screen_res_);
    }
  }
}

void VideoPlaneController::Pause() {
  DCHECK(thread_checker_.CalledOnValidThread());
  VLOG(1) << "Pausing controller. No more VideoPlane SetGeometry calls.";
  is_paused_ = true;
  //VINOD: TODO: Check whether this is needed or not
  for (auto it=video_window_map_.begin(); it!=video_window_map_.end(); ++it) {
    if(it->second) {
      it->second->Pause();
    }
  }
}

void VideoPlaneController::Resume() {
  DCHECK(thread_checker_.CalledOnValidThread());
  VLOG(1) << "Resuming controller. VideoPlane SetGeometry calls are active.";
  is_paused_ = false;
  //VINOD: TODO: Check whether this is needed or not
  for (auto it=video_window_map_.begin(); it!=video_window_map_.end(); ++it) {
    if(it->second) {
      it->second->Resume();
    }
  }
}

bool VideoPlaneController::is_paused() const {
  DCHECK(thread_checker_.CalledOnValidThread());
  return is_paused_;
}

bool VideoPlaneController::HaveDataForSetGeometry() const {
  return have_screen_res_ && have_video_plane_geometry_;
}

void VideoPlaneController::ClearVideoPlaneGeometry() {
  have_video_plane_geometry_ = false;
  video_plane_display_rect_ = RectF(0, 0);
  video_plane_transform_ = VideoPlane::TRANSFORM_NONE;
}

void VideoPlaneController::AddVideoWindow(const std::string &key,
                                         VideoWindowController* vwc) {
  auto iter = video_window_map_.find(key);

  if(iter == video_window_map_.end()) {
    //Insert
    video_window_map_.insert(std::make_pair(key, vwc));
  } else {
    //Update
    video_window_map_[key] = vwc;
  }
  if(have_screen_res_) {
    vwc->SetScreenResolution(screen_res_);
  }
}

void VideoPlaneController::RemoveVideoWindow(const std::string &key) {
  auto iter = video_window_map_.find(key);

  if(iter != video_window_map_.end()) {
    video_window_map_.erase(key);
  }
}

}  // namespace media
}  // namespace chromecast
