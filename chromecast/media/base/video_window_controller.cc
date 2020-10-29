// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromecast/media/base/video_window_controller.h"

#include <stddef.h>
#include <stdint.h>
#include <vector>

#include "base/bind.h"
#include "base/location.h"
#include "base/macros.h"
#include "base/single_thread_task_runner.h"
#include "base/time/time.h"
#include "chromecast/media/cma/backend/cma_backend.h"

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

// Translates a gfx::OverlayTransform into a VideoWindow::Transform.
// Could be just a lookup table once we have unit tests for this code
// to ensure it stays in sync with OverlayTransform.
chromecast::media::VideoWindow::Transform ConvertTransform(
    gfx::OverlayTransform transform) {
  switch (transform) {
    case gfx::OVERLAY_TRANSFORM_NONE:
      return chromecast::media::VideoWindow::TRANSFORM_NONE;
    case gfx::OVERLAY_TRANSFORM_FLIP_HORIZONTAL:
      return chromecast::media::VideoWindow::FLIP_HORIZONTAL;
    case gfx::OVERLAY_TRANSFORM_FLIP_VERTICAL:
      return chromecast::media::VideoWindow::FLIP_VERTICAL;
    case gfx::OVERLAY_TRANSFORM_ROTATE_90:
      return chromecast::media::VideoWindow::ROTATE_90;
    case gfx::OVERLAY_TRANSFORM_ROTATE_180:
      return chromecast::media::VideoWindow::ROTATE_180;
    case gfx::OVERLAY_TRANSFORM_ROTATE_270:
      return chromecast::media::VideoWindow::ROTATE_270;
    default:
      NOTREACHED();
      return chromecast::media::VideoWindow::TRANSFORM_NONE;
  }
}

}  // namespace

// Helper class for calling VideoWindow::SetGeometry with rate-limiting.
// SetGeometry can take on the order of 100ms to run in some implementations
// and can be called on the order of 20x / second (as fast as graphics frames
// are produced).  This creates an ever-growing backlog of tasks on the media
// thread.
// This class measures the time taken to run SetGeometry to determine a
// reasonable frequency at which to call it.  Excess calls are coalesced
// to just set the most recent geometry.
class VideoWindowController::RateLimitedSetVideoWindowGeometry
    : public base::RefCountedThreadSafe<RateLimitedSetVideoWindowGeometry> {
 public:
  RateLimitedSetVideoWindowGeometry(
      CmaBackend *cma_backend,
      const scoped_refptr<base::SingleThreadTaskRunner>& task_runner)
      : cma_backend_(cma_backend),
        pending_display_rect_(0, 0, 0, 0),
        pending_set_geometry_(false),
        min_calling_interval_ms_(0),
        sample_counter_(0),
        task_runner_(task_runner) {}

  void SetGeometry(const chromecast::RectF& display_rect,
                   VideoWindow::Transform transform) {
    DCHECK(task_runner_->BelongsToCurrentThread());
    DCHECK(DisplayRectFValid(display_rect));

    base::TimeTicks now = base::TimeTicks::Now();
    base::TimeDelta elapsed = now - last_set_geometry_time_;

    if (elapsed < base::TimeDelta::FromMilliseconds(min_calling_interval_ms_)) {
      if (!pending_set_geometry_) {
        pending_set_geometry_ = true;

        task_runner_->PostDelayedTask(
            FROM_HERE,
            base::BindOnce(
                &RateLimitedSetVideoWindowGeometry::ApplyPendingSetGeometry,
                this),
            base::TimeDelta::FromMilliseconds(2 * min_calling_interval_ms_));
      }

      pending_display_rect_ = display_rect;
      pending_transform_ = transform;
      return;
    }
    last_set_geometry_time_ = now;

    LOG(INFO) << __FUNCTION__ << " rect=" << display_rect.width << "x"
              << display_rect.height << " @" << display_rect.x << ","
              << display_rect.y << " transform " << transform;

    base::TimeTicks start = base::TimeTicks::Now();
    if(cma_backend_->GetVideoWindow())
      cma_backend_->GetVideoWindow()->SetGeometry(display_rect, transform);
    else
      LOG(ERROR) << "Failed to get from the backend.";

    base::TimeDelta set_geometry_time = base::TimeTicks::Now() - start;
    UpdateAverageTime(set_geometry_time.InMilliseconds());
  }

 private:
  friend class base::RefCountedThreadSafe<RateLimitedSetVideoWindowGeometry>;
  ~RateLimitedSetVideoWindowGeometry() {}

  void UpdateAverageTime(int64_t sample) {
    const size_t kSampleCount = 5;
    if (samples_.size() < kSampleCount)
      samples_.push_back(sample);
    else
      samples_[sample_counter_++ % kSampleCount] = sample;
    int64_t total = 0;
    for (int64_t s : samples_)
      total += s;
    min_calling_interval_ms_ = 2 * total / samples_.size();
  }

  void ApplyPendingSetGeometry() {
    if (pending_set_geometry_) {
      pending_set_geometry_ = false;
      SetGeometry(pending_display_rect_, pending_transform_);
    }
  }

  //Corrsponding underlying CMABackend
  CmaBackend *cma_backend_;

  RectF pending_display_rect_;
  VideoWindow::Transform pending_transform_;
  bool pending_set_geometry_;
  base::TimeTicks last_set_geometry_time_;

  // Don't call SetGeometry faster than this interval.
  int64_t min_calling_interval_ms_;

  // Min calling interval is computed as double average of last few time samples
  // (i.e. allow at least as much time between calls as the call itself takes).
  std::vector<int64_t> samples_;
  size_t sample_counter_;

  scoped_refptr<base::SingleThreadTaskRunner> task_runner_;

  DISALLOW_COPY_AND_ASSIGN(RateLimitedSetVideoWindowGeometry);
};

VideoWindowController::VideoWindowController(
    CmaBackend *backend,
    const Size& graphics_resolution,
    scoped_refptr<base::SingleThreadTaskRunner> media_task_runner)
    : is_paused_(false),
      have_screen_res_(true),
      screen_res_(1920, 1080),
      graphics_window_res_(graphics_resolution),
      have_video_window_geometry_(false),
      video_window_display_rect_(0, 0),
      video_window_transform_(VideoWindow::TRANSFORM_NONE),
      media_task_runner_(media_task_runner),
      video_window_wrapper_(
          new RateLimitedSetVideoWindowGeometry(backend,
          media_task_runner_)) {
}

VideoWindowController::~VideoWindowController() {}

void VideoWindowController::SetGeometry(const gfx::RectF& gfx_display_rect,
                                       gfx::OverlayTransform gfx_transform) {
  const RectF display_rect(gfx_display_rect.x(), gfx_display_rect.y(),
                           gfx_display_rect.width(), gfx_display_rect.height());
  VideoWindow::Transform transform = ConvertTransform(gfx_transform);

//  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(DisplayRectFValid(display_rect));
  if (have_video_window_geometry_ &&
      RectFEqual(display_rect, video_window_display_rect_) &&
      transform == video_window_transform_) {
    VLOG(1) << "No change found in geometry parameters.";
    return;
  }

  LOG(INFO) << "New geometry parameters "
          << " rect=" << display_rect.width << "x" << display_rect.height
          << " @" << display_rect.x << "," << display_rect.y << " transform "
          << transform;

  have_video_window_geometry_ = true;
  video_window_display_rect_ = display_rect;
  video_window_transform_ = transform;

  MaybeRunSetGeometry();
}

void VideoWindowController::SetScreenResolution(const Size& resolution) {
//  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(ResolutionSizeValid(resolution));
  if (have_screen_res_ && SizeEqual(resolution, screen_res_)) {
    VLOG(2) << "No change found in screen resolution.";
    return;
  }

  VLOG(1) << "New screen resolution " << resolution.width << "x"
          << resolution.height;

  have_screen_res_ = true;
  screen_res_ = resolution;

  MaybeRunSetGeometry();
}

void VideoWindowController::Pause() {
//  DCHECK(thread_checker_.CalledOnValidThread());
  VLOG(1) << "Pausing controller. No more VideoWindow SetGeometry calls.";
  is_paused_ = true;
}

void VideoWindowController::Resume() {
//  DCHECK(thread_checker_.CalledOnValidThread());
  VLOG(1) << "Resuming controller. VideoWindow SetGeometry calls are active.";
  is_paused_ = false;
  ClearVideoWindowGeometry();
}

bool VideoWindowController::is_paused() const {
//  DCHECK(thread_checker_.CalledOnValidThread());
  return is_paused_;
}

void VideoWindowController::MaybeRunSetGeometry() {
  if (is_paused_) {
    LOG(INFO) << "All VideoWindow SetGeometry calls are paused."
              << " Ignoring request.\nhave_screen_res="<<have_screen_res_
              << ", have_video_window_geometry="<<have_video_window_geometry_;
    return;
  }

  if (!HaveDataForSetGeometry()) {
    LOG(INFO) << "Don't have all VideoWindow SetGeometry data. Ignoring request.";
    return;
  }

  DCHECK(graphics_window_res_.width != 0 && graphics_window_res_.height != 0);

  RectF scaled_rect = video_window_display_rect_;
  if (graphics_window_res_.width != screen_res_.width ||
      graphics_window_res_.height != screen_res_.height) {
    float sx =
        static_cast<float>(screen_res_.width) / graphics_window_res_.width;
    float sy =
        static_cast<float>(screen_res_.height) / graphics_window_res_.height;
    scaled_rect.x *= sx;
    scaled_rect.y *= sy;
    scaled_rect.width *= sx;
    scaled_rect.height *= sy;
  }

  media_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&RateLimitedSetVideoWindowGeometry::SetGeometry,
                                video_window_wrapper_, scaled_rect,
                                video_window_transform_));
}

bool VideoWindowController::HaveDataForSetGeometry() const {
  return have_screen_res_ && have_video_window_geometry_;
}

void VideoWindowController::ClearVideoWindowGeometry() {
  VLOG(1) << "VideoWindowController::" << __func__;
  have_video_window_geometry_ = false;
  video_window_display_rect_ = RectF(0, 0);
  video_window_transform_ = VideoWindow::TRANSFORM_NONE;
}

}  // namespace media
}  // namespace chromecast
