// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_FILTERS_FRAME_DEMUXER_H_
#define MEDIA_FILTERS_FRAME_DEMUXER_H_

#include <stddef.h>
#include <stdint.h>

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/containers/circular_deque.h"
#include "base/macros.h"
#include "base/memory/memory_pressure_listener.h"
#include "base/synchronization/lock.h"
#include "base/thread_annotations.h"
#include "media/base/byte_queue.h"
#include "media/base/demuxer.h"
#include "media/base/demuxer_stream.h"
#include "media/base/media_tracks.h"
#include "media/base/ranges.h"
#include "media/base/stream_parser.h"
#include "media/base/audio_decoder_config.h"
#include "media/base/video_decoder_config.h"
#include "media/base/decoder_buffer.h"
#include "media/base/decoder_buffer_queue.h"
#include "media/filters/source_buffer_parse_warnings.h"
#include "media/filters/source_buffer_state.h"
#include "media/filters/source_buffer_stream.h"

namespace media {
class VideoFrame;

class MEDIA_EXPORT FrameDemuxerStream : public DemuxerStream {
 public:
  using BufferQueue = base::circular_deque<scoped_refptr<StreamParserBuffer>>;

  FrameDemuxerStream(Type type, MediaTrack::Id media_track_id);
  ~FrameDemuxerStream() override;

  // DemuxerStream methods.
  void Read(const ReadCB& read_cb) override;
  Type type() const override;
  Liveness liveness() const override;
  AudioDecoderConfig audio_decoder_config() override;
  VideoDecoderConfig video_decoder_config() override;
  bool SupportsConfigChanges() override;
  void NotifyLastPtsSentToDecoder(const int64_t last_sent_pts); // override;

  bool UpdateAudioConfig();
  bool UpdateVideoConfig();
  void EnqueuePacket(scoped_refptr<media::VideoFrame> frame, bool first_frame);
  void SatisfyPendingRead();

 private:
  enum State {
    UNINITIALIZED,
    RETURNING_DATA_FOR_READS,
    RETURNING_ABORT_FOR_READS,
    SHUTDOWN,
  };

  // Specifies the type of the stream.
  const Type type_;

  std::unique_ptr<AudioDecoderConfig> audio_config_;
  std::unique_ptr<VideoDecoderConfig> video_config_;

  Liveness liveness_ GUARDED_BY(lock_);

  const MediaTrack::Id media_track_id_;
  DecoderBufferQueue buffer_queue_;

  mutable base::Lock lock_;
  State state_ GUARDED_BY(lock_);
  ReadCB read_cb_ GUARDED_BY(lock_);
  bool is_enabled_ GUARDED_BY(lock_);

  DISALLOW_IMPLICIT_CONSTRUCTORS(FrameDemuxerStream);
};

// Demuxer implementation that allows MediaFrames of media data to be
// passed from webrtc/capture devices to the media stack.
class MEDIA_EXPORT FrameDemuxer : public Demuxer {
 public:
  enum Status {
    kOk,              // ID added w/o error.
    kNotSupported,    // Type specified is not supported.
    kReachedIdLimit,  // Reached ID limit. We can't handle any more IDs.
  };

  // |open_cb| Run when Initialize() is called to signal that the demuxer
  //   is ready to receive media data via AppendData().
  // |progress_cb| Run each time data is appended.
  // |media_log| Used to report content and engine debug messages.
  FrameDemuxer(const base::Closure& open_cb,
               const base::Closure& progress_cb,
               MediaLog* media_log);
  ~FrameDemuxer() override;

  // Demuxer implementation.
  std::string GetDisplayName() const override;

  // |enable_text| Process inband text tracks in the normal way when true,
  //   otherwise ignore them.
  void Initialize(DemuxerHost* host, const PipelineStatusCB& init_cb) override;
  void Stop() override;
  void Seek(base::TimeDelta time, const PipelineStatusCB& cb) override;
  base::Time GetTimelineOffset() const override;
  std::vector<DemuxerStream*> GetAllStreams() override;
  base::TimeDelta GetStartTime() const override;
  int64_t GetMemoryUsage() const override;
  void AbortPendingReads() override;

  // FrameDemuxer reads are abortable. StartWaitingForSeek() and
  // CancelPendingSeek() always abort pending and future reads until the
  // expected seek occurs, so that FrameDemuxer can stay synchronized with the
  // associated JS method calls.
  void StartWaitingForSeek(base::TimeDelta seek_time) override;
  void CancelPendingSeek(base::TimeDelta seek_time) override;

  void OnEnabledAudioTracksChanged(const std::vector<MediaTrack::Id>& track_ids,
                                   base::TimeDelta curr_time,
                                   TrackChangeCB change_completed_cb) override;

  void OnSelectedVideoTrackChanged(const std::vector<MediaTrack::Id>& track_ids,
                                   base::TimeDelta curr_time,
                                   TrackChangeCB change_completed_cb) override;

  void EnqueueFrame(scoped_refptr<media::VideoFrame> frame);

 private:
  enum State {
    WAITING_FOR_INIT = 0,
    INITIALIZING,
    INITIALIZED,
    ENDED,

    // Any State at or beyond PARSE_ERROR cannot be changed to a state before
    // this. See ChangeState_Locked.
    PARSE_ERROR,
    SHUTDOWN,
  };

  void RunInitCB_Locked(PipelineStatus status);

  mutable base::Lock lock_;
  State state_;

  DemuxerHost* host_;
  base::Closure open_cb_;
  base::Closure progress_cb_;

  // MediaLog for reporting messages and properties to debug content and engine.
  MediaLog* media_log_;

  PipelineStatusCB init_cb_;
  // Callback to execute upon seek completion.
  // TODO(wolenetz/acolwell): Protect against possible double-locking by first
  // releasing |lock_| before executing this callback. See
  // http://crbug.com/308226
  PipelineStatusCB seek_cb_;

  using OwnedFrameDemuxerStream = std::unique_ptr<FrameDemuxerStream>;
  OwnedFrameDemuxerStream audio_stream_;
  OwnedFrameDemuxerStream video_stream_;

  // Keep track of which ids still remain uninitialized so that we transition
  // into the INITIALIZED only after all ids/SourceBuffers got init segment.
  std::set<std::string> pending_source_init_ids_;

  base::TimeDelta duration_;

  base::Time timeline_offset_;

  std::map<std::string, std::unique_ptr<SourceBufferState>> source_state_map_;

  std::map<std::string, std::vector<FrameDemuxerStream*>> id_to_streams_map_;
  // Used to hold alive the demuxer streams that were created for removed /
  // released SourceBufferState objects. Demuxer clients might still have
  // references to these streams, so we need to keep them alive. But they'll be
  // in a shut down state, so reading from them will return EOS.
  std::vector<std::unique_ptr<FrameDemuxerStream>> removed_streams_;

  // Accumulate, by type, detected track counts across the SourceBuffers.
//  int detected_audio_track_count_;
//  int detected_video_track_count_;

  std::map<MediaTrack::Id, FrameDemuxerStream*> track_id_to_demux_stream_map_;

  bool first_frame_received_ = false;

  DISALLOW_COPY_AND_ASSIGN(FrameDemuxer);
};

}  // namespace media

#endif  // MEDIA_FILTERS_FRAME_DEMUXER_H_
