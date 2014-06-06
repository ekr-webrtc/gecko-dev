/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// Class templates copied from WebRTC:
/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */


#ifndef WEBRTCGMPVIDEOCODEC_H_
#define WEBRTCGMPVIDEOCODEC_H_

#include <sys/time.h>

#include <iostream>
#include <queue>

#include "nsThreadUtils.h"
#include "mozilla/Mutex.h"

#include "mozIGeckoMediaPluginService.h"
#include "MediaConduitInterface.h"
#include "AudioConduit.h"
#include "VideoConduit.h"
#include "webrtc/modules/video_coding/codecs/interface/video_codec_interface.h"

#include "gmp-video-host.h"
#include "gmp-video-encode.h"
#include "gmp-video-decode.h"
#include "gmp-video-frame-i420.h"
#include "gmp-video-frame-encoded.h"

#include "WebrtcGmpVideoCodec.h"

namespace mozilla {

class WebrtcGmpVideoEncoder : public WebrtcVideoEncoder,
                              public GMPEncoderCallback
{
public:
  WebrtcGmpVideoEncoder();
  virtual ~WebrtcGmpVideoEncoder() {}

  // Implement VideoEncoder interface.
  virtual int32_t InitEncode(const webrtc::VideoCodec* aCodecSettings,
                             int32_t aNumberOfCores,
                             uint32_t aMaxPayloadSize);

  virtual int32_t Encode(const webrtc::I420VideoFrame& aInputImage,
                         const webrtc::CodecSpecificInfo* aCodecSpecificInfo,
                         const std::vector<webrtc::VideoFrameType>* aFrameTypes);

  virtual int32_t RegisterEncodeCompleteCallback(
    webrtc::EncodedImageCallback* aCallback);

  virtual int32_t Release();

  virtual int32_t SetChannelParameters(uint32_t aPacketLoss,
                                       int aRTT);

  virtual int32_t SetRates(uint32_t aNewBitRate,
                           uint32_t aFrameRate);

  // GMPEncoderCallback virtual functions.
  virtual void Encoded(GMPVideoEncodedFrame* aEncodedFrame,
                       const GMPCodecSpecificInfo& aCodecSpecificInfo);


private:
  virtual int32_t InitEncode_g(const webrtc::VideoCodec* aCodecSettings,
                               int32_t aNumberOfCores,
                               uint32_t aMaxPayloadSize);

  virtual int32_t Encode_g(const webrtc::I420VideoFrame* aInputImage,
                           const webrtc::CodecSpecificInfo* aCodecSpecificInfo,
                           const std::vector<webrtc::VideoFrameType>* aFrameTypes);

  virtual int32_t SetRates_g(uint32_t aNewBitRate,
                             uint32_t aFrameRate);

  int32_t GetNextNALUnit(const uint8_t **aData, size_t *aSize,
                         const uint8_t **aNalStart, size_t *aNalSize,
                         bool aStartCodeFollows);

  nsCOMPtr<mozIGeckoMediaPluginService> mMPS;
  nsIThread* mGMPThread;
  GMPVideoEncoder* mGMP;
  GMPVideoHost* mHost;
  webrtc::EncodedImageCallback* mCallback;
};


class WebrtcGmpVideoDecoder : public WebrtcVideoDecoder,
                              public GMPDecoderCallback
{
public:
  WebrtcGmpVideoDecoder();
  virtual ~WebrtcGmpVideoDecoder() {}

  // Implement VideoDecoder interface.
  virtual int32_t InitDecode(const webrtc::VideoCodec* aCodecSettings,
                             int32_t aNumberOfCores);
  virtual int32_t Decode(const webrtc::EncodedImage& aInputImage,
                         bool aMissingFrames,
                         const webrtc::RTPFragmentationHeader* aFragmentation,
                         const webrtc::CodecSpecificInfo* aCodecSpecificInfo = nullptr,
                         int64_t aRenderTimeMs = -1);
  virtual int32_t RegisterDecodeCompleteCallback(webrtc::DecodedImageCallback* aCallback);

  virtual int32_t Release();

  virtual int32_t Reset();

  virtual void Decoded(GMPVideoi420Frame* aDecodedFrame);

  virtual void ReceivedDecodedReferenceFrame(const uint64_t aPictureId) {
    MOZ_CRASH();
  }

  virtual void ReceivedDecodedFrame(const uint64_t aPictureId) {
    MOZ_CRASH();
  }

  virtual void InputDataExhausted() {
    MOZ_CRASH();
  }

private:
  virtual int32_t InitDecode_g(const webrtc::VideoCodec* aCodecSettings,
                               int32_t aNumberOfCores);

  virtual int32_t Decode_g(const webrtc::EncodedImage& aInputImage,
                           bool aMissingFrames,
                           const webrtc::RTPFragmentationHeader* aFragmentation,
                           const webrtc::CodecSpecificInfo* aCodecSpecificInfo,
                           int64_t aRenderTimeMs);

  nsCOMPtr<mozIGeckoMediaPluginService> mMPS;
  nsIThread* mGMPThread;
  GMPVideoDecoder* mGMP;
  GMPVideoHost* mHost;
  webrtc::DecodedImageCallback* mCallback;
};

}

#endif
