// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMECAST_MEDIA_VIDEO_WINDOW_CONTROLLER_H_
#define CHROMECAST_MEDIA_VIDEO_WINDOW_CONTROLLER_H_

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/singleton.h"
#include "base/threading/thread_checker.h"
#include "chromecast/public/graphics_types.h"
#include "chromecast/public/video_window.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/overlay_transform.h"

namespace base {
class SingleThreadTaskRunner;
}

namespace chromecast {
namespace media {
class CmaBackend;

// Provides main interface for setting video window geometry.  All callsites
// should use this over VideoWindow::SetGeometry.  Reasons for this:
// * provides conversion between graphics window coordinates and screen
//   resolution coordinates
// * updates VideoWindow when screen resolution changes
// * handles threading correctly (posting SetGeometry to media thread).
// * coalesces multiple calls in short space of time to prevent flooding the
//   media thread with SetGeometry calls (which are expensive on many
//   platforms).
// All public methods should be called from the same thread that the class was
// constructed on.
class VideoWindowController {
 public:
  VideoWindowController(
      CmaBackend *backend,
      const Size& graphics_resolution,
      scoped_refptr<base::SingleThreadTaskRunner> media_task_runner);
  ~VideoWindowController();

  // Sets the video window geometry in *graphics window coordinates*. If there is
  // no change to video window parameters from the last call to this method, it
  // is a no-op.
  void SetGeometry(const gfx::RectF& display_rect,
                   gfx::OverlayTransform transform);

  // Sets physical screen resolution. This must be called at least once when
  // the final output resolution (HDMI signal or panel resolution) is known,
  // then later when it changes. If there is no change to the screen resolution
  // from the last call to this method, it is a no-op.
  void SetScreenResolution(const Size& resolution);

  // After Pause is called, no further calls to VideoWindow::SetGeometry will be
  // made except for any pending calls already scheduled on the media thread.
  // The Set methods will however update cached parameters that will take
  // effect once the class is resumed. Safe to call multiple times.
  // TODO(esum): Handle the case where there are pending calls already on the
  // media thread. When this returns, the caller needs to know that absolutely
  // no more SetGeometry calls will be made.
  void Pause();
  // Makes class active again, and clears any cached video window geometry
  // parameters. Safe to call multiple times.
  void Resume();
  bool is_paused() const;
  void ClearVideoWindowGeometry();

 private:
  class RateLimitedSetVideoWindowGeometry;
  friend struct base::DefaultSingletonTraits<VideoWindowController>;

  // Check if HaveDataForSetGeometry. If not, this method is a no-op. Otherwise
  // it scales the display rect from graphics to device resolution coordinates.
  // Then posts task to media thread for VideoWindow::SetGeometry.
  void MaybeRunSetGeometry();
  // Checks if all data has been collected to make calls to
  // VideoWindow::SetGeometry.
  bool HaveDataForSetGeometry() const;
  // Clears any cached video window geometry parameters.
  //void ClearVideoWindowGeometry();

  bool is_paused_;

  // Current resolutions
  bool have_screen_res_;
  Size screen_res_;
  const Size graphics_window_res_;

  // Saved video window parameters (in graphics window coordinates)
  // for use when screen resolution changes.
  bool have_video_window_geometry_;
  RectF video_window_display_rect_;
  VideoWindow::Transform video_window_transform_;

  scoped_refptr<base::SingleThreadTaskRunner> media_task_runner_;
  scoped_refptr<RateLimitedSetVideoWindowGeometry> video_window_wrapper_;

  base::ThreadChecker thread_checker_;

  DISALLOW_COPY_AND_ASSIGN(VideoWindowController);
};

}  // namespace media
}  // namespace chromecast

#endif  // CHROMECAST_MEDIA_VIDEO_WINDOW_CONTROLLER_H_
