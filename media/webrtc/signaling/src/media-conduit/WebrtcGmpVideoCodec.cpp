/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebrtcGmpVideoCodec.h"

#include <iostream>
#include <vector>

#include "mozilla/Scoped.h"
#include "VideoConduit.h"
#include "AudioConduit.h"
#include "runnable_utils.h"

#include "mozIGeckoMediaPluginService.h"
#include "nsServiceManagerUtils.h"

#include "gmp-video-host.h"
#include "gmp-video-frame-i420.h"
#include "gmp-video-frame-encoded.h"

#include "webrtc/video_engine/include/vie_external_codec.h"

namespace mozilla {

// Encoder.
WebrtcGmpVideoEncoder::WebrtcGmpVideoEncoder() :
    mMPS(nullptr),
    mGMPThread(nullptr),
    mGMP(nullptr),
    mHost(nullptr),
    mCallback(nullptr) {}

static int
WebrtcFrameTypeToGmpFrameType(webrtc::VideoFrameType aIn,
                              GMPVideoFrameType *aOut)
{
  // XXX Array lookup?
  switch(aIn) {
    case webrtc::kKeyFrame:
      *aOut = kGMPKeyFrame;
      break;
    case webrtc::kDeltaFrame:
      *aOut = kGMPDeltaFrame;
      break;
    case webrtc::kGoldenFrame:
      *aOut = kGMPGoldenFrame;
      break;
    case webrtc::kAltRefFrame:
      *aOut = kGMPAltRefFrame;
      break;
    case webrtc::kSkipFrame:
      *aOut = kGMPSkipFrame;
      break;
    default:
      MOZ_CRASH();
      return WEBRTC_VIDEO_CODEC_ERROR;
  }

  return WEBRTC_VIDEO_CODEC_OK;
}

static int
GmpFrameTypeToWebrtcFrameType(GMPVideoFrameType aIn,
                              webrtc::VideoFrameType *aOut)
{
  // XXX Array lookup?
  switch(aIn) {
    case kGMPKeyFrame:
      *aOut = webrtc::kKeyFrame;
      break;
    case kGMPDeltaFrame:
      *aOut = webrtc::kDeltaFrame;
      break;
    case kGMPGoldenFrame:
      *aOut = webrtc::kGoldenFrame;
      break;
    case kGMPAltRefFrame:
      *aOut = webrtc::kAltRefFrame;
      break;
    case kGMPSkipFrame:
      *aOut = webrtc::kSkipFrame;
      break;
    default:
      MOZ_CRASH();
      return WEBRTC_VIDEO_CODEC_ERROR;
  }

  return WEBRTC_VIDEO_CODEC_OK;
}


int32_t WebrtcGmpVideoEncoder::InitEncode(const webrtc::VideoCodec* aCodecSettings,
                                          int32_t aNumberOfCores,
                                          uint32_t aMaxPayloadSize)
{
  std::cerr << "EKR: InitEncode\n";
  mMPS = do_GetService("@mozilla.org/gecko-media-plugin-service;1");

  if (!mMPS) {
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  nsresult rv = mMPS->GetThread(&mGMPThread);
  if (NS_FAILED(rv)) {
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  int32_t ret;
  RUN_ON_THREAD(mGMPThread,
                WrapRunnableRet(this,
                                &WebrtcGmpVideoEncoder::InitEncode_g,
                                aCodecSettings,
                                aNumberOfCores,
                                aMaxPayloadSize,
                                &ret));

  return ret;
}

int32_t
WebrtcGmpVideoEncoder::InitEncode_g(const webrtc::VideoCodec* aCodecSettings,
                                    int32_t aNumberOfCores,
                                    uint32_t aMaxPayloadSize)
{
  GMPVideoHost* host = nullptr;
  GMPVideoEncoder* gmp = nullptr;

  nsresult rv = mMPS->GetGMPVideoEncoderVP8(&host, &gmp);
  if (NS_FAILED(rv)) {
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  mGMP = gmp;
  mHost = host;

  if (!gmp || !host) {
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  // TODO(ekr@rtfm.com): transfer settings from codecSettings to codec.
  GMPVideoCodec codec;
  memset(&codec, 0, sizeof(codec));

  codec.mWidth = aCodecSettings->width;
  codec.mHeight = aCodecSettings->height;
  codec.mStartBitrate = aCodecSettings->startBitrate;
  codec.mMinBitrate = aCodecSettings->minBitrate;
  codec.mMaxBitrate = aCodecSettings->maxBitrate;
  codec.mMaxFramerate = aCodecSettings->maxFramerate;

  GMPVideoErr err = mGMP->InitEncode(codec, this, 1, aMaxPayloadSize);
  if (err != GMPVideoNoErr) {
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  return WEBRTC_VIDEO_CODEC_OK;
}


int32_t
WebrtcGmpVideoEncoder::Encode(const webrtc::I420VideoFrame& aInputImage,
                              const webrtc::CodecSpecificInfo* aCodecSpecificInfo,
                              const std::vector<webrtc::VideoFrameType>* aFrameTypes)
{
  int32_t ret;
  RUN_ON_THREAD(mGMPThread,
                WrapRunnableRet(this,
                                &WebrtcGmpVideoEncoder::Encode_g,
                                &aInputImage,
                                aCodecSpecificInfo,
                                aFrameTypes,
                                &ret));

  return ret;
}


int32_t
WebrtcGmpVideoEncoder::Encode_g(const webrtc::I420VideoFrame* aInputImage,
                                const webrtc::CodecSpecificInfo* aCodecSpecificInfo,
                                const std::vector<webrtc::VideoFrameType>* aFrameTypes)
{
  GMPVideoFrame* ftmp = nullptr;
  // Translate the image.
  GMPVideoErr err = mHost->CreateFrame(kGMPI420VideoFrame, &ftmp);
  if (err != GMPVideoNoErr) {
    return WEBRTC_VIDEO_CODEC_ERROR;
  }
  GMPVideoi420Frame* frame = static_cast<GMPVideoi420Frame*>(ftmp);

  err = frame->CreateFrame(aInputImage->allocated_size(webrtc::kYPlane),
                           aInputImage->buffer(webrtc::kYPlane),
                           aInputImage->allocated_size(webrtc::kUPlane),
                           aInputImage->buffer(webrtc::kUPlane),
                           aInputImage->allocated_size(webrtc::kVPlane),
                           aInputImage->buffer(webrtc::kVPlane),
                           aInputImage->width(),
                           aInputImage->height(),
                           aInputImage->stride(webrtc::kYPlane),
                           aInputImage->stride(webrtc::kUPlane),
                           aInputImage->stride(webrtc::kVPlane));
  if (err != GMPVideoNoErr) {
    return err;
  }
  frame->SetTimestamp(aInputImage->timestamp());
  frame->SetRenderTime_ms(aInputImage->render_time_ms());

  // TODO(ekr@rtfm.com): actually translate this.
  GMPCodecSpecificInfo info;
  memset(&info, 0, sizeof(info));

  std::vector<GMPVideoFrameType> gmp_frame_types;
  for (auto it = aFrameTypes->begin(); it != aFrameTypes->end(); ++it) {
    GMPVideoFrameType ft;

    int32_t ret = WebrtcFrameTypeToGmpFrameType(*it, &ft);
    if (ret != WEBRTC_VIDEO_CODEC_OK) {
      return ret;
    }

    gmp_frame_types.push_back(ft);
  }

  err = mGMP->Encode(frame, info, gmp_frame_types);
  if (err != GMPVideoNoErr) {
    return err;
  }

  return WEBRTC_VIDEO_CODEC_OK;
}



int32_t
WebrtcGmpVideoEncoder::RegisterEncodeCompleteCallback(webrtc::EncodedImageCallback* aCallback)
{
  mCallback = aCallback;

  return WEBRTC_VIDEO_CODEC_OK;
}

int32_t
WebrtcGmpVideoEncoder::Release()
{
  return WEBRTC_VIDEO_CODEC_OK;
}

int32_t
WebrtcGmpVideoEncoder::SetChannelParameters(uint32_t aPacketLoss, int aRTT)
{
  return WEBRTC_VIDEO_CODEC_OK;
}

int32_t
WebrtcGmpVideoEncoder::SetRates(uint32_t aNewBitRate, uint32_t aFrameRate)
{
  int32_t ret;
  RUN_ON_THREAD(mGMPThread,
                WrapRunnableRet(this,
                                &WebrtcGmpVideoEncoder::SetRates_g,
                                aNewBitRate, aFrameRate,
                                &ret));

  return ret;
}

int32_t
WebrtcGmpVideoEncoder::SetRates_g(uint32_t aNewBitRate, uint32_t aFrameRate)
{
  GMPVideoErr err = mGMP->SetRates(aNewBitRate, aFrameRate);
  if (err != GMPVideoNoErr) {
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  return WEBRTC_VIDEO_CODEC_OK;
}

// Is there any way to get the encoder to provide the NALs in the other
// format (<size><nal><size><nal> etc), so we don't have to scan for
// start codes?
// Also, if not: can this loop be simplified?  There's no description of the
// data it's required to handle...
int32_t
WebrtcGmpVideoEncoder::GetNextNALUnit(const uint8_t **aData,
                                      size_t *aSize,
                                      const uint8_t **aNalStart,
                                      size_t *aNalSize,
                                      bool aStartCodeFollows) // always true?
{
  const uint8_t *data = *aData;
  size_t size = *aSize;
  *aNalStart = nullptr;
  *aNalSize = 0;

  if (size == 0) {
    return -1;
  }

  // Skip any number of leading 0x00.

  size_t offset = 0;
  while (offset < size && data[offset] == 0x00) {
    ++offset;
  }

  if (offset == size) {
    return -1;
  }

  // A valid startcode consists of at least two 0x00 bytes followed by 0x01.

  if (offset < 2 || data[offset] != 0x01) {
    return -2;
  }

  ++offset;

  size_t startOffset = offset;

  for (;;) {
    while (offset < size && data[offset] != 0x01) {
      ++offset;
    }

    // XXX this deserves at least a comment
    if (offset == size) {
      if (aStartCodeFollows) {
        offset = size + 2;
        break;
      }

      return -1;
    }
    // We know offset > 2
    if (data[offset - 1] == 0x00 && data[offset - 2] == 0x00) {
      break;
    }

    ++offset;
  }

  size_t endOffset = offset - 2;
  while (endOffset > startOffset + 1 && data[endOffset - 1] == 0x00) {
    --endOffset;
  }

  *aNalStart = &data[startOffset];
  *aNalSize = endOffset - startOffset;

  if (offset + 2 < size) {
    *aData = &data[offset - 2];
    *aSize = size - offset + 2;
  } else {
    *aData = NULL;
    *aSize = 0;
  }

  return 0;
}

// GMPEncoderCallback virtual functions.
void
WebrtcGmpVideoEncoder::Encoded(GMPVideoEncodedFrame* aEncodedFrame,
                               const GMPCodecSpecificInfo& aCodecSpecificInfo)
{
  webrtc::VideoFrameType ft;
  GmpFrameTypeToWebrtcFrameType(aEncodedFrame->FrameType(), &ft);

  webrtc::EncodedImage nalu(aEncodedFrame->Buffer(), aEncodedFrame->AllocatedSize(),
                            aEncodedFrame->Size());

  // Break input encoded data into NALUs and send each one to callback.
  const uint8_t* data = aEncodedFrame->Buffer();
  size_t size = aEncodedFrame->Size();
  const uint8_t* nalStart = nullptr;
  size_t nalSize = 0;
  // XXX Why always true for GetNextNALUnit?
  while (GetNextNALUnit(&data, &size, &nalStart, &nalSize, true) == 0) {
    nalu._buffer = const_cast<uint8_t*>(nalStart);
    nalu._length = nalSize;
    nalu._frameType = ft;
    nalu._timeStamp = aEncodedFrame->TimeStamp();
    nalu._completeFrame = true;
    mCallback->Encoded(nalu, nullptr, nullptr);
  }

  aEncodedFrame->Destroy();
}


// Decoder.
WebrtcGmpVideoDecoder::WebrtcGmpVideoDecoder() :
  mMPS(nullptr),
  mGMPThread(nullptr),
  mGMP(nullptr),
  mHost(nullptr),
  mCallback(nullptr) {}

int32_t
WebrtcGmpVideoDecoder::InitDecode(const webrtc::VideoCodec* aCodecSettings,
                                  int32_t aNumberOfCores)
{
  mMPS = do_GetService("@mozilla.org/gecko-media-plugin-service;1");
  if (!mMPS) {
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  if (NS_WARN_IF(NS_FAILED(mMPS->GetThread(&mGMPThread))))
  {
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  int32_t ret;
  RUN_ON_THREAD(mGMPThread,
                WrapRunnableRet(this,
                                &WebrtcGmpVideoDecoder::InitDecode_g,
                                aCodecSettings,
                                aNumberOfCores,
                                &ret));

  return ret;
}

int32_t
WebrtcGmpVideoDecoder::InitDecode_g(const webrtc::VideoCodec* aCodecSettings,
                                    int32_t aNumberOfCores)
{
  GMPVideoHost* host = nullptr;
  GMPVideoDecoder* gmp = nullptr;

  if (NS_WARN_IF(NS_FAILED(mMPS->GetGMPVideoDecoderVP8(&host, &gmp))))
  {
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  mGMP = gmp;
  mHost = host;
  if (!gmp || !host) {
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  // TODO(ekr@rtfm.com): transfer settings from aCodecSettings to codec.
  GMPVideoCodec codec;
  memset(&codec, 0, sizeof(codec));

  GMPVideoErr err = mGMP->InitDecode(codec, this, 1); // XXX 1?
  if (err != GMPVideoNoErr) {
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  return WEBRTC_VIDEO_CODEC_OK;
}

int32_t
WebrtcGmpVideoDecoder::Decode(const webrtc::EncodedImage& aInputImage,
                              bool aMissingFrames,
                              const webrtc::RTPFragmentationHeader* aFragmentation,
                              const webrtc::CodecSpecificInfo* aCodecSpecificInfo,
                              int64_t aRenderTimeMs)
{
  int32_t ret;
  RUN_ON_THREAD(mGMPThread,
                WrapRunnableRet(this,
                                &WebrtcGmpVideoDecoder::Decode_g,
                                aInputImage,
                                aMissingFrames,
                                aFragmentation,
                                aCodecSpecificInfo,
                                aRenderTimeMs,
                                &ret));

  return ret;
}

int32_t
WebrtcGmpVideoDecoder::Decode_g(const webrtc::EncodedImage& aInputImage,
                                bool aMissingFrames,
                                const webrtc::RTPFragmentationHeader* aFragmentation,
                                const webrtc::CodecSpecificInfo* aCodecSpecificInfo,
                                int64_t aRenderTimeMs)
{
  GMPVideoFrame* ftmp = nullptr;
  GMPVideoErr err = mHost->CreateFrame(kGMPEncodedVideoFrame, &ftmp);
  if (err != GMPVideoNoErr) {
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  GMPVideoEncodedFrame* frame = static_cast<GMPVideoEncodedFrame*>(ftmp);
  err = frame->CreateEmptyFrame(aInputImage._length);
  if (err != GMPVideoNoErr) {
    return WEBRTC_VIDEO_CODEC_ERROR;
  }
  // XXX It'd be wonderful not to have to memcpy the encoded data!
  memcpy(frame->Buffer(), aInputImage._buffer, frame->Size());

  frame->SetEncodedWidth(aInputImage._encodedWidth);
  frame->SetEncodedHeight(aInputImage._encodedHeight);
  frame->SetTimeStamp(aInputImage._timeStamp);
  frame->SetCompleteFrame(aInputImage._completeFrame);

  GMPVideoFrameType ft;
  int32_t ret = WebrtcFrameTypeToGmpFrameType(aInputImage._frameType, &ft);
  if (ret != WEBRTC_VIDEO_CODEC_OK) {
    return ret;
  }

  // TODO(ekr@rtfm.com): Fill in
  GMPCodecSpecificInfo info;
  memset(&info, 0, sizeof(info));

  err = mGMP->Decode(frame, aMissingFrames, info, aRenderTimeMs);
  if (err != GMPVideoNoErr) {
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  return WEBRTC_VIDEO_CODEC_OK;
}

int32_t
WebrtcGmpVideoDecoder::RegisterDecodeCompleteCallback( webrtc::DecodedImageCallback* aCallback)
{
  mCallback = aCallback;

  return WEBRTC_VIDEO_CODEC_OK;
}


int32_t
WebrtcGmpVideoDecoder::Release()
{
  return WEBRTC_VIDEO_CODEC_OK;
}

int32_t
WebrtcGmpVideoDecoder::Reset()
{
  return WEBRTC_VIDEO_CODEC_OK;
}

void
WebrtcGmpVideoDecoder::Decoded(GMPVideoi420Frame* aDecodedFrame)
{
  webrtc::I420VideoFrame image;
  int ret = image.CreateFrame(aDecodedFrame->AllocatedSize(kGMPYPlane),
                              aDecodedFrame->Buffer(kGMPYPlane),
                              aDecodedFrame->AllocatedSize(kGMPUPlane),
                              aDecodedFrame->Buffer(kGMPUPlane),
                              aDecodedFrame->AllocatedSize(kGMPVPlane),
                              aDecodedFrame->Buffer(kGMPVPlane),
                              aDecodedFrame->Width(),
                              aDecodedFrame->Height(),
                              aDecodedFrame->Stride(kGMPYPlane),
                              aDecodedFrame->Stride(kGMPUPlane),
                              aDecodedFrame->Stride(kGMPVPlane));
  if (ret != 0) {
    return;
  }
  image.set_timestamp(aDecodedFrame->Timestamp());
  image.set_render_time_ms(0);

  mCallback->Decoded(image);
  aDecodedFrame->Destroy();
}

}
