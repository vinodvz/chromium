// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/filters/frame_demuxer.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <utility>

#include "base/bind.h"
#include "base/callback_helpers.h"
#include "base/location.h"
#include "base/macros.h"
#include "base/metrics/histogram_macros.h"
#include "base/strings/string_number_conversions.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/trace_event/trace_event.h"
#include "media/base/bind_to_current_loop.h"
#include "media/base/media_switches.h"
#include "media/base/media_tracks.h"
#include "media/base/mime_util.h"
#include "media/base/stream_parser_buffer.h"
#include "media/base/timestamp_constants.h"
#include "media/base/video_codecs.h"
#include "media/base/video_frame.h"
#include "media/filters/frame_processor.h"
#include "media/filters/source_buffer_stream.h"
#include "media/filters/stream_parser_factory.h"

#include "media/base/encryption_scheme.h"
#include "media/base/media_util.h"
#include "media/base/video_util.h"

using base::TimeDelta;

namespace media {

FrameDemuxerStream::FrameDemuxerStream(Type type,
                                       MediaTrack::Id media_track_id)
    : type_(type),
      liveness_(DemuxerStream::LIVENESS_UNKNOWN),
      media_track_id_(media_track_id),
      state_(UNINITIALIZED),
      is_enabled_(true) {}

// DemuxerStream methods.
void FrameDemuxerStream::Read(const ReadCB& read_cb) {
  base::AutoLock auto_lock(lock_);
  DCHECK_NE(state_, UNINITIALIZED);
  DCHECK(!read_cb_);

  read_cb_ = BindToCurrentLoop(read_cb);

  if (!is_enabled_) {
	DVLOG(1) << "Read from disabled stream, returning EOS ";
    std::move(read_cb_).Run(kOk, StreamParserBuffer::CreateEOSBuffer());
    return;
  }
  if (!buffer_queue_.IsEmpty()) {
	// LOG(ERROR) << " Returning buffer.";
     std::move(read_cb_).Run(DemuxerStream::kOk, buffer_queue_.Pop());
  } else {
	 // LOG(ERROR) << " No buffer to return.";
  }
}

DemuxerStream::Type FrameDemuxerStream::type() const { return type_; }

DemuxerStream::Liveness FrameDemuxerStream::liveness() const {
  base::AutoLock auto_lock(lock_);
  return liveness_;
}

AudioDecoderConfig FrameDemuxerStream::audio_decoder_config() {
  DCHECK(audio_config_.get());
  return *audio_config_;
}

VideoDecoderConfig FrameDemuxerStream::video_decoder_config() {
  DCHECK(video_config_.get());
  return *video_config_;
}

bool FrameDemuxerStream::SupportsConfigChanges() { return true; }

void FrameDemuxerStream::NotifyLastPtsSentToDecoder(const int64_t last_sent_pts) {
}

bool FrameDemuxerStream::UpdateAudioConfig() {
  base::AutoLock auto_lock(lock_);

  return true;
}

bool FrameDemuxerStream::UpdateVideoConfig() {
  base::AutoLock auto_lock(lock_);
  LOG(ERROR) << "FrameDemuxerStream::UpdateVideoConfig()";

	VideoCodec codec = kCodecH264;
	VideoCodecProfile profile = H264PROFILE_HIGH;
	VideoPixelFormat format = PIXEL_FORMAT_I420;
	VideoColorSpace color_space = VideoColorSpace::REC709();
	VideoRotation video_rotation = VIDEO_ROTATION_0;
//	AVRational aspect_ratio = {1, 1};

	gfx::Rect visible_rect(640, 480);
	gfx::Size coded_size = visible_rect.size();
	// Pad out |coded_size| for subsampled YUV formats.
	if (format != PIXEL_FORMAT_I444 && format != PIXEL_FORMAT_UNKNOWN) {
	  coded_size.set_width((coded_size.width() + 1) / 2 * 2);
	  if (format != PIXEL_FORMAT_I422)
		coded_size.set_height((coded_size.height() + 1) / 2 * 2);
	}
	gfx::Size natural_size =
		GetNaturalSize(visible_rect.size(), 1 /*aspect_ratio.numerator*/, 1 /*aspect_ratio.den*/);

	std::vector<uint8_t> extra_data;
	EncryptionScheme en_scheme = Unencrypted();
    
  video_config_.reset(new VideoDecoderConfig());
  video_config_->Initialize(codec, profile, format, color_space, video_rotation,
                     coded_size, visible_rect, natural_size, extra_data,
                     en_scheme);
  return true;
}

void FrameDemuxerStream::EnqueuePacket(scoped_refptr<media::VideoFrame> frame, bool first_frame) {
  scoped_refptr<DecoderBuffer> buffer;

  buffer = DecoderBuffer::CopyFrom(frame->data(0),
									 frame->data_size());
  buffer->set_timestamp(frame->timestamp());
  buffer->set_duration(base::TimeDelta::FromMilliseconds(33)); // VINOD: It is 30fps. Fix it

  if(first_frame)
	  buffer->set_is_key_frame(true); //VINOD: We may have to parse H.264 frames and look for I-frames

  {
	  base::AutoLock auto_lock(lock_);
	  buffer_queue_.Push(std::move(buffer));
	  state_= RETURNING_DATA_FOR_READS;
  }
  //LOG(ERROR) << " Buffer added.";
  SatisfyPendingRead();
}

void FrameDemuxerStream::SatisfyPendingRead() {
  base::AutoLock auto_lock(lock_);
  if (read_cb_) {
    if (!buffer_queue_.IsEmpty()) {
  //	 LOG(ERROR) << "Returning buffer.";
      std::move(read_cb_).Run(DemuxerStream::kOk, buffer_queue_.Pop());
    }
  }
}

FrameDemuxerStream::~FrameDemuxerStream() = default;

FrameDemuxer::FrameDemuxer(
    const base::Closure& open_cb,
    const base::Closure& progress_cb,
    MediaLog* media_log)
    : open_cb_(open_cb),
      progress_cb_(progress_cb),
      media_log_(media_log) {
//  DCHECK(open_cb_);
  LOG(ERROR) << "FrameDemuxer::FrameDemuxer()";
}

std::string FrameDemuxer::GetDisplayName() const {
  return "FrameDemuxer";
}

void FrameDemuxer::Initialize(DemuxerHost* host,
                              const PipelineStatusCB& init_cb) {
  LOG(ERROR) << "FrameDemuxer::Initialize()";
  TRACE_EVENT_ASYNC_BEGIN0("media", "FrameDemuxer::Initialize", this);

  host_ = host;
  // Do not post init_cb once this function returns because if there is an
  // error after initialization, the error might be reported before init_cb
  // has a chance to run. This is because FrameDemuxer::ReportError_Locked
  // directly calls DemuxerHost::OnDemuxerError: crbug.com/633016.
  init_cb_ = init_cb;
  video_stream_.reset(new FrameDemuxerStream(DemuxerStream::VIDEO,base::UintToString(1)));
  video_stream_->UpdateVideoConfig();

  std::move(open_cb_).Run();
}

void FrameDemuxer::Stop() {
	LOG(ERROR) << "FrameDemuxer::Stop()";
}

void FrameDemuxer::Seek(TimeDelta time, const PipelineStatusCB& cb) {
	LOG(ERROR) << "FrameDemuxer::Seek(" << time.InSecondsF() << ")";
}

// Demuxer implementation.
base::Time FrameDemuxer::GetTimelineOffset() const {
  return timeline_offset_;
}

std::vector<DemuxerStream*> FrameDemuxer::GetAllStreams() {
  base::AutoLock auto_lock(lock_);
  std::vector<DemuxerStream*> result;
  // Put enabled streams at the beginning of the list so that
  // MediaResource::GetFirstStream returns the enabled stream if there is one.
  // TODO(servolk): Revisit this after media track switching is supported.
//  result.push_back(audio_stream_.get());
  result.push_back(video_stream_.get());
  return result;
}

TimeDelta FrameDemuxer::GetStartTime() const {
  return TimeDelta();
}

int64_t FrameDemuxer::GetMemoryUsage() const {
  base::AutoLock auto_lock(lock_);
  int64_t mem = 0;
//  return buffer_queue_.data_size();
  return mem;
}

void FrameDemuxer::AbortPendingReads() {

}

void FrameDemuxer::StartWaitingForSeek(TimeDelta seek_time) {
  MEDIA_LOG(ERROR, media_log_) << "StartWaitingForSeek() :"<<state_;
}

void FrameDemuxer::CancelPendingSeek(TimeDelta seek_time) {

}

void FrameDemuxer::OnEnabledAudioTracksChanged(
	const std::vector<MediaTrack::Id>& track_ids,
	base::TimeDelta curr_time,
	TrackChangeCB change_completed_cb) {

}

void FrameDemuxer::OnSelectedVideoTrackChanged(
    const std::vector<MediaTrack::Id>& track_ids,
    base::TimeDelta curr_time,
    TrackChangeCB change_completed_cb) {

}

FrameDemuxer::~FrameDemuxer() {
}

void FrameDemuxer::EnqueueFrame(scoped_refptr<media::VideoFrame> frame) {
  //MEDIA_LOG(ERROR, media_log_) << "Received new frame.";
  video_stream_->EnqueuePacket(frame, !first_frame_received_);

  if(!first_frame_received_) {
	  first_frame_received_ = true;
	  RunInitCB_Locked(PIPELINE_OK);
  }
}

void FrameDemuxer::RunInitCB_Locked(PipelineStatus status) {
//  lock_.AssertAcquired();
	base::AutoLock auto_lock(lock_);
  DCHECK(init_cb_);
  TRACE_EVENT_ASYNC_END1("media", "FrameDemuxer::Initialize", this, "status",
                         MediaLog::PipelineStatusToString(status));
  LOG(ERROR) <<" VINOD: FrameDemuxer::RunInitCB_Locked => "<<MediaLog::PipelineStatusToString(status);
  std::move(init_cb_).Run(status);
}

}  // namespace media
