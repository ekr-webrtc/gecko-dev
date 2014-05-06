#include "CSFLog.h"

#include <iostream>
#include "nsICommandLine.h"
#include "VideoBenchmark.h"

#include <string>
#include <sys/time.h>
#include <sys/resource.h>

#include "mozilla/Atomics.h"
#include "mozilla/Scoped.h"
#include "nsIEventTarget.h"
#include <MediaConduitInterface.h>
#include "OMXVideoCodec.h"
#include "gfxPlatform.h"

#ifdef MOZILLA_INTERNAL_API
#include "nsString.h"
#endif

#define LOG_TAG "WebrtcVideoBenchmark"
#define CODEC_LOGV(...) CSFLogInfo(LOG_TAG, __VA_ARGS__)
#define CODEC_LOGD(...) CSFLogDebug(LOG_TAG, __VA_ARGS__)
#define CODEC_LOGI(...) CSFLogInfo(LOG_TAG, __VA_ARGS__)
#define CODEC_LOGW(...) CSFLogWarn(LOG_TAG, __VA_ARGS__)
#define CODEC_LOGE(...) CSFLogError(LOG_TAG, __VA_ARGS__)

namespace mozilla {

static std::string g_input_file;
static std::string g_encoded_output_file;
FILE *g_encoded_output_fp = nullptr;
static int g_width = 640;
static int g_height = 480;
static int g_frame_rate = 30;
static int g_total_frames = -1;
static bool g_send_and_receive = false;


uint64_t timeval2int(const struct timeval &tv) {
  uint64_t ret = tv.tv_sec;
  ret *= 1000000;
  ret += tv.tv_usec;

  return ret;
}

uint64_t time64() {
  struct timeval tv;

  gettimeofday(&tv, NULL);
  return timeval2int(tv);
}

void getrtimes(uint64_t *utime, uint64_t *stime) {
  struct rusage ru;

  int r = getrusage(RUSAGE_SELF, &ru);
  if (r < 0) {
    exit(1);
  }

  *utime = timeval2int(ru.ru_utime);
  *stime = timeval2int(ru.ru_stime);
}

class Benchmark;

class YuvReader {
 public:
  YuvReader()
  : input_fp_(nullptr),
    init_(false),
    height_(0),
    width_(0),
    frame_size_(0),
    frame_(nullptr),
    loop_(false) {}
  ~YuvReader() {
    if (input_fp_)
      fclose(input_fp_);
  }

  bool Init(const std::string& input_file, bool loop) {
    std::cerr << "Initializing YuvReader with file " << input_file
	      << " loop=" << loop << std::endl;
    loop_ = loop;
    input_fp_ = fopen(input_file.c_str(), "r");
    if (!input_fp_)
      return false;

    char line[1024];
    if (!fgets(line, sizeof(line), input_fp_))
      return false;

    if (sscanf(line, "YUV4MPEG2 W%d H%d", &width_, &height_) != 2)
      return false;

    if (width_ <= 0 && width_ > 2000)
      return false;

    if (height_ <= 0 && height_ > 2000)
      return false;

    frame_size_ = (width_ * height_ * 3) / 2;
    g_height = height_;
    g_width = width_;
    frame_ = new unsigned char[frame_size_];
    init_ = true;

    std::cerr << "Initialized\n";

    return true;
  }

  bool ReadFrame() {
    int retry_ct = loop_ ? 2 : 1;

    while (retry_ct > 0) {
      --retry_ct;

      char frame_hdr[1024];
      if (fgets(frame_hdr, sizeof(frame_hdr), input_fp_)) {
	if (strncmp(frame_hdr, "FRAME", 5)) {
	  std::cerr << "Bogus data" << std::endl;
	  return false;
	}

	int r = fread(frame_, 1, frame_size_, input_fp_);
	if (r == frame_size_)
	  return true;
      }

      // Loop
      std::cerr << "Rewinding" << std::endl;
      rewind(input_fp_);
      // Read the first line.
      if (!fgets(frame_hdr, sizeof(frame_hdr), input_fp_)){
	std::cerr << "Couldn't read first line" << std::endl;
        return false;
      }
    }

    return false;
  }

  unsigned char *frame() { return frame_.get(); }
  int frame_size() { return frame_size_; }
  int height() { return height_; }
  int width() { return width_; }

 private:
  FILE *input_fp_;
  bool init_;
  int height_;
  int width_;
  int frame_size_;
  ScopedDeleteArray<unsigned char> frame_;
  bool loop_;
};


class Transport : public mozilla::TransportInterface {
 public:
  virtual nsresult SendRtpPacket(const void* data, int len)
  {
    CODEC_LOGE("Encoded packet of length %d", len);
    if (g_encoded_output_fp) {
      uint32_t ll = len;
      ll = htonl(ll);
      fwrite(&len, 1, 4, g_encoded_output_fp);
      fwrite(data, 1, len, g_encoded_output_fp);
    }

    if (receiver_) {
      receiver_->ReceivedRTPPacket(data, len);
    }
    return NS_OK;
  }

  virtual nsresult SendRtcpPacket(const void* data, int len)
  {
    if (receiver_) {
      receiver_->ReceivedRTCPPacket(data, len);
    }
    return NS_OK;
  }

  void SetReceiver(mozilla::RefPtr<mozilla::VideoSessionConduit> receiver) {
    receiver_ = receiver;
  }

 private:
  mozilla::RefPtr<mozilla::VideoSessionConduit> receiver_;
};

class Renderer : public VideoRenderer {
 public:
  Renderer(Benchmark *parent) : parent_(parent) {}

  virtual void RenderVideoFrame(const unsigned char* buffer,
                                unsigned int buffer_size,
                                uint32_t time_stamp,
                                int64_t render_time,
				const ImageHandle& handle);
  void FrameSizeChange(unsigned int, unsigned int, unsigned int) {}

 private:
  Benchmark *parent_;
};

class Benchmark {
 public:
  static Benchmark* Create(const std::string& input_file) {
    ScopedDeletePtr<Benchmark> b(new Benchmark(input_file));
    if (!b->Init())
      return nullptr;

    return b.forget();
  }

  ~Benchmark() {
  }

  void Describe() {
    std::cerr << "Running benchmark from file " << g_input_file << std::endl;
    std::cerr << "Frame count: " << total_frames_ << std::endl;
    std::cerr << "Frame rate: " << frame_rate_ << std::endl;
    std::cerr << "Frame size: " << width_ << "x" << height_ << std::endl;
    std::cout << "Frame\tProc.Time\tUser.Time\tSystem.Time\tBacklog" << std::endl;
  }

  void Run() {
    Describe();
    for (;;) {
      uint64_t t0 = time64();

      if (!ProcessFrame())
        return;

      uint64_t t1 = time64();
      uint64_t elapsed_ms = (t1 - t0) / 1000;
      if (elapsed_ms > interframe_time_) {
        std::cerr << "Frame took too long to process" << std::endl;
      } else {
        PR_Sleep(PR_MillisecondsToInterval(interframe_time_ - elapsed_ms));
      }
    }
  }

  bool ProcessFrame() {
    if (!reader_.ReadFrame()) {
      std::cerr << "No more data" << std::endl;
      return false;
    }

    uint64_t t0 = time64();
    std::cout << "TIMEin:\t" << frame_ct_ << "\t" << t0 << std::endl;

    int i = 0;
    for (int b = 0; b < 10; b++){
      int bit = (frame_ct_ & ( 1 << i )) >> i;
      i++;
      //printf("%d", bit);
      for (int y = 0; y < 2; y++) {
        for (int x = 0; x < 2; x++) {
          reader_.frame()[b*2+ (y*width_ + x)] = (char)(bit<<7);
//          std::cout<< "inbit " << b*2+ (y*width_ + x)<< ": \t"<< (int)reader_.frame()[b*2+ (y*width_ + x)] << std::endl;
        }
      }
    }

    CODEC_LOGE("Inserting frame");

    //printf("\n");
    int err = sender_->SendVideoFrame(reader_.frame(),
                                      reader_.frame_size(),
                                      width_,
                                      height_,
                                      mozilla::kVideoI420,
                                      0);
    if (err != mozilla::kMediaConduitNoError) {
      std::cerr << "Error sending video frame" << std::endl;
    }

    uint64_t t1 = time64();
    uint64_t ut, st;
    getrtimes(&ut, &st);
    sent_frame();

    std::cout << frame_ct_ << "\t"
              << t1 - t0 << "\t"
              << ut - utime_ << "\t"
              << st - stime_ << "\t"
              << frames_outstanding_ << "\n";

    utime_ = ut;
    stime_ = st;

    if ((total_frames_ != -1) && (frame_ct_ >= total_frames_)) {
      printf("Sleep\n");
      PR_Sleep(1000);
      std::cout << "Read " << total_frames_ << "... finished" << std::endl;
      return false;
    }

    return true;
  }

  bool SetupReception() {
    std::cerr << "Setting up video for receive\n";

    if (!(receiver_ = mozilla::VideoSessionConduit::Create(nullptr)))
      return false;

    external_decoder_ = OMXVideoCodec::CreateDecoder(OMXVideoCodec::CODEC_H264);
    if (!external_decoder_)
      return false;

    int err = receiver_->SetExternalRecvCodec(124, external_decoder_);
    if (err != mozilla::kMediaConduitNoError)
      return false;

    mozilla::VideoCodecConfig cinst1(120, "VP8", 0);
    mozilla::VideoCodecConfig cinst2(124, "I420", 0);
    std::vector<mozilla::VideoCodecConfig*> configs;
    configs.push_back(&cinst1);
    configs.push_back(&cinst2);
    err = receiver_->ConfigureRecvMediaCodecs(configs);
    if (err != mozilla::kMediaConduitNoError)
      return false;

    renderer_ = new Renderer(this);
    err = receiver_->AttachRenderer(renderer_);
    if (err != mozilla::kMediaConduitNoError)
      return false;

    sender_transport_->SetReceiver(receiver_);

    std::cerr << "Receive setup complete\n";
    return true;
  }

  void sent_frame() {
    ++frame_ct_;
    ++frames_outstanding_;
  }

  void received_frame() {
    --frames_outstanding_;
  }

 private:
  Benchmark(const std::string& input_file)
      : reader_(),
        input_file_(input_file),
        sender_(nullptr),
        sender_transport_(new Transport()),
        receiver_(nullptr),
        width_(0),
        height_(0),
        frame_rate_(g_frame_rate),
        frame_ct_(0),
        total_frames_(g_total_frames),
        loop_(total_frames_ > 0),
        interframe_time_(1000 / g_frame_rate),
        frames_outstanding_(0),
        utime_(0),
        stime_(0) {}

  bool Init() {
    if (!(reader_.Init(input_file_, loop_)))
      return false;

    height_ = reader_.height();
    width_ = reader_.width();

    if (!(sender_ = mozilla::VideoSessionConduit::Create(nullptr)))
      return false;

    std::cerr << "Created conduit\n";
    
    external_encoder_ = OMXVideoCodec::CreateEncoder(OMXVideoCodec::CODEC_H264);
    if (!external_encoder_)
      return false;

    std::cerr << "Created external encoder\n";

    int err = sender_->SetExternalSendCodec(124, external_encoder_);
    if (err != mozilla::kMediaConduitNoError)
      return false;
    
    std::cerr << "Set send codec encoder\n";
    //mozilla::VideoCodecConfig cinst1(120, "VP8", 0);
    mozilla::VideoCodecConfig cinst1(124, "I420", 0);
    err = sender_->ConfigureSendMediaCodec(&cinst1);
    if (err != mozilla::kMediaConduitNoError)
      return false;

    err = sender_->AttachTransport(sender_transport_);
    if (err != mozilla::kMediaConduitNoError)
      return false;

    std::cerr << "Benchmark init complete\n";
    return true;
  }

  YuvReader reader_;
  const std::string& input_file_;
  mozilla::RefPtr<mozilla::VideoSessionConduit> sender_;
  mozilla::RefPtr<Transport> sender_transport_;
  mozilla::RefPtr<mozilla::VideoSessionConduit> receiver_;
  mozilla::RefPtr<mozilla::VideoRenderer> renderer_;
  mozilla::ScopedDeletePtr<mozilla::VideoEncoder> external_encoder_;
  mozilla::ScopedDeletePtr<mozilla::VideoDecoder> external_decoder_;

  int width_;
  int height_;
  int frame_rate_;
  int frame_ct_;
  int total_frames_;
  bool loop_;
  uint64_t interframe_time_;
  mozilla::Atomic<uint32_t> frames_outstanding_;
  uint64_t utime_;
  uint64_t stime_;
};

void Renderer::RenderVideoFrame(const unsigned char* buffer,
                                unsigned int buffer_size,
                                uint32_t time_stamp,
                                int64_t render_time,
				const ImageHandle& handle) {
  std::cerr << "Rendered\n";
#if 0
  int total =0;
  for (int b=0; b < 10; b++){
    int sum = 0;
    for (int y = 0; y < 2; y++) {
      for (int x = 0; x < 2; x++) {
        sum += (buffer[b*2 + y*g_width + x]);
      }
    }
    if(sum/4 > 60){
      //printf("1");
      total += 1 << b;
    } else {
      //printf("0");
    }
  }
  //printf("\n");
#endif
  uint64_t timeout = time64();
  std::cout << "TIMEout:\t" << timeout << std::endl;
  parent_->received_frame();
}

/* Implementation file */
NS_IMPL_ISUPPORTS(VideoBenchmark, nsICommandLineHandler)

VideoBenchmark::VideoBenchmark()
{
  /* member initializers and constructor code */
}

VideoBenchmark::~VideoBenchmark()
{
  /* destructor code */
}

std::string GetArgument(const nsAString& flag, nsICommandLine *cmdline) {
#ifdef MOZILLA_INTERNAL_API
  nsresult rv;
  nsString result;

  rv = cmdline->HandleFlagWithParam(flag, false, result);
  if (NS_FAILED(rv))
    return "";

  return ToNewCString(result);
#else
  return "";
#endif
}

/* void handle (in nsICommandLine aCommandLine); */
NS_IMETHODIMP VideoBenchmark::Handle(nsICommandLine *aCommandLine)
{
#ifdef MOZILLA_INTERNAL_API
    bool found;
    nsresult rv;
    NS_NAMED_LITERAL_STRING(benchmark_flag, "video-benchmark");
    rv = aCommandLine->HandleFlag(benchmark_flag, false, &found);
    if (!NS_SUCCEEDED(rv))
	return rv;
    if (!found)
	return NS_OK;

    std::cerr << "Running video benchmark \n";

    gfxPlatform::GetPlatform();

    g_input_file = GetArgument(NS_LITERAL_STRING("video-benchmark-file"),
                               aCommandLine);

    g_encoded_output_file = GetArgument(NS_LITERAL_STRING("video-encoding-file"),
					aCommandLine);
    if (g_encoded_output_file.size()) {
      g_encoded_output_fp = fopen(g_encoded_output_file.c_str(), "w");
      if (g_encoded_output_fp) {
	std::cerr << "Outputting RTP to " << g_encoded_output_file << std::endl;
      }
    }

    std::string tmp = GetArgument(NS_LITERAL_STRING("video-benchmark-frames"),
                                  aCommandLine);
    if (tmp.size())
      g_total_frames = atoi(tmp.c_str());

    tmp = GetArgument(NS_LITERAL_STRING("video-benchmark-framerate"),
                      aCommandLine);
    if (tmp.size())
      g_frame_rate = atoi(tmp.c_str());

    rv = aCommandLine->HandleFlag(NS_LITERAL_STRING("video-benchmark-receive"), false, &found);
    g_send_and_receive = found;

    ScopedDeletePtr<Benchmark> benchmark(Benchmark::Create(g_input_file));
    if (!benchmark) {
	std::cerr << "Couldn't create benchmark" << std::endl;
	exit(1);
    }
    if (g_send_and_receive)
	benchmark->SetupReception();
    uint64_t t0 = time64();
    uint64_t ut0, st0;
    getrtimes(&ut0, &st0);

    benchmark->Run();
    uint64_t t1 = time64();

    PR_Sleep(10000);

    uint64_t ut1, st1;
    getrtimes(&ut1, &st1);
    std::cout << "SYSTEM: " << st1-st0 << std::endl;
    std::cout << "USER: " << ut1 - ut0 << std::endl;
    std::cout << "TIME: " << t1 - t0 << std::endl;

    exit(0);
#endif
    return NS_OK;
}

/* readonly attribute AUTF8String helpInfo; */
NS_IMETHODIMP VideoBenchmark::GetHelpInfo(nsACString & aHelpInfo)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}

}
