// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMECAST_PUBLIC_VIDEO_WINDOW_H_
#define CHROMECAST_PUBLIC_VIDEO_WINDOW_H_

namespace chromecast {
struct RectF;

namespace media {

// Abstract surface of individual video, it should be implemented in
// platform-specific way. Platform must composite all the windows
// together on top of main video plane.
class VideoWindow {
 public:
  // List of possible hardware transforms that can be applied to video.
  // Rotations are anti-clockwise.
  enum Transform {
    TRANSFORM_NONE,
    ROTATE_90,
    ROTATE_180,
    ROTATE_270,
    FLIP_HORIZONTAL,
    FLIP_VERTICAL,
  };

  virtual ~VideoWindow() {}

  // Updates the video window geometry.
  // |screen_rect| specifies the rectangle that the video should occupy,
  // in screen resolution coordinates.
  // |transform| specifies how the video should be transformed within that
  // rectangle.
  virtual void SetGeometry(const RectF& screen_rect, Transform transform) = 0;

  // Getting current z-order of the video window
  virtual int GetZOrder() = 0;

  // Setting z-order of the video window
  virtual void SetZOrder(int z_order) = 0;

  // Mute or Unmute video rendering. (Won't affect audio)
  //    muted = true : Disables the video decoding & drawing.
  //    muted = false: Enables the video decoding & drawing.
  //
  // NOTE: In muted state, platform may free-up resources allocated for video.
  virtual void SetVideoMute(bool muted) = 0;
};

}  // namespace media
}  // namespace chromecast

#endif  // CHROMECAST_PUBLIC_VIDEO_WINDOW_H_
