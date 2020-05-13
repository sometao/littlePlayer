/**
@author Tao Zhang
@since 2020/3/1
@version 0.0.1-SNAPSHOT 2020/5/13
*/
#pragma once

#ifdef _WIN32
// Windows
extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libavutil/imgutils.h"
#include "libswresample/swresample.h"
};
#else
// Linux...
#ifdef __cplusplus
extern "C" {
#endif
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libswresample/swresample.h>
#ifdef __cplusplus
};
#endif
#endif

#include <string>
#include <iostream>
#include <sstream>
#include <tuple>

namespace ffmpegUtil {

using std::cout;
using std::endl;
using std::string;
using std::stringstream;

struct ffUtils {
  static void initCodecContext(AVFormatContext* f, int streamIndex, AVCodecContext** ctx) {
    string codecTypeStr{};
    switch (f->streams[streamIndex]->codec->codec_type) {
      case AVMEDIA_TYPE_VIDEO:
        codecTypeStr = "vidoe_decodec";
        break;
      case AVMEDIA_TYPE_AUDIO:
        codecTypeStr = "audio_decodec";
        break;
      default:
        throw std::runtime_error("error_decodec, it should not happen.");
    }

    AVCodec* codec = avcodec_find_decoder(f->streams[streamIndex]->codecpar->codec_id);

    if (codec == nullptr) {
      string errorMsg = "Could not find codec: ";
      errorMsg += f->streams[streamIndex]->codecpar->codec_id;
      cout << errorMsg << endl;
      throw std::runtime_error(errorMsg);
    }

    (*ctx) = avcodec_alloc_context3(codec);
    auto codecCtx = *ctx;

    if (avcodec_parameters_to_context(codecCtx, f->streams[streamIndex]->codecpar) != 0) {
      string errorMsg = "Could not copy codec context: ";
      errorMsg += codec->name;
      cout << errorMsg << endl;
      throw std::runtime_error(errorMsg);
    }

    if (avcodec_open2(codecCtx, codec, nullptr) < 0) {
      string errorMsg = "Could not open codec: ";
      errorMsg += codec->name;
      cout << errorMsg << endl;
      throw std::runtime_error(errorMsg);
    }

    cout << codecTypeStr << " [" << codecCtx->codec->name
         << "] codec context initialize success." << endl;
  }
};

class PacketGrabber {
  const string inputUrl;
  AVFormatContext* formatCtx = nullptr;
  bool fileGotToEnd = false;

  int videoIndex = -1;
  int audioIndex = -1;

 public:
  ~PacketGrabber() { 
    if (formatCtx != nullptr) {
      avformat_free_context(formatCtx);
      formatCtx = nullptr;
    }
    cout << "~PacketGrabber called." << endl; 
  }

  PacketGrabber(const string& uri) : inputUrl(uri) {

    formatCtx = avformat_alloc_context();
    if (avformat_open_input(&formatCtx, inputUrl.c_str(), NULL, NULL) != 0) {
      string errorMsg = "Can not open input file:";
      errorMsg += inputUrl;
      cout << errorMsg << endl;
      throw std::runtime_error(errorMsg);
    }

    if (avformat_find_stream_info(formatCtx, NULL) < 0) {
      string errorMsg = "Can not find stream information in input file:";
      errorMsg += inputUrl;
      cout << errorMsg << endl;
      throw std::runtime_error(errorMsg);
    }

    for (int i = 0; i < formatCtx->nb_streams; i++) {
      if (formatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO && videoIndex == -1) {
        videoIndex = i;
        cout << "video stream index = : [" << i << "]" << endl;
      }

      if (formatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO && audioIndex == -1) {
        audioIndex = i;
        cout << "audio stream index = : [" << i << "]" << endl;
      }
    }
  }

  /*
   *  return
   *          x > 0  : stream_index
   *          -1     : no more pkt
   */
  int grabPacket(AVPacket* pkt) {
    if (fileGotToEnd) {
      return -1;
    }
    while (true) {
      if (av_read_frame(formatCtx, pkt) >= 0) {
        return pkt->stream_index;
      } else {
        // file end;
        fileGotToEnd = true;
        return -1;
      }
    }
  }

  AVFormatContext* getFormatCtx() const { return formatCtx; }

  bool isFileEnd() const { return fileGotToEnd; }

  int getAudioIndex() const { return audioIndex; }
  int getVideoIndex() const { return videoIndex; }
};


struct AudioInfo {
  int64_t layout;
  int sampleRate;
  int channels;
  AVSampleFormat format;

  AudioInfo() {
    layout = -1;
    sampleRate = -1;
    channels = -1;
    format = AV_SAMPLE_FMT_S16;
  }

  AudioInfo(int64_t l, int rate, int c, AVSampleFormat f)
      : layout(l), sampleRate(rate), channels(c), format(f) {}
};

class ReSampler {
  SwrContext* swr;

 public:
  ReSampler(const ReSampler&) = delete;
  ReSampler(ReSampler&&) noexcept = delete;
  ReSampler operator=(const ReSampler&) = delete;
  ~ReSampler() {
    cout << "~ReSampler called." << endl;
    if (swr != nullptr) {
      swr_free(&swr);
    }
  }

  const AudioInfo in;
  const AudioInfo out;

  static AudioInfo getDefaultAudioInfo(int sr) {
    int64_t layout = AV_CH_LAYOUT_STEREO;
    int sampleRate = sr;
    int channels = 2;
    AVSampleFormat format = AV_SAMPLE_FMT_S16;

    return ffmpegUtil::AudioInfo(layout, sampleRate, channels, format);
  }

  ReSampler(AudioInfo input, AudioInfo output) : in(input), out(output) {
    swr = swr_alloc_set_opts(nullptr, out.layout, out.format, out.sampleRate, in.layout,
                             in.format, in.sampleRate, 0, nullptr);

    if (swr_init(swr)) {
      throw std::runtime_error("swr_init error.");
    }
  }

  int allocDataBuf(uint8_t** outData, int inputSamples) {
    int bytePerOutSample = -1;
    switch (out.format) {
      case AV_SAMPLE_FMT_U8:
        bytePerOutSample = 1;
        break;
      case AV_SAMPLE_FMT_S16P:
      case AV_SAMPLE_FMT_S16:
        bytePerOutSample = 2;
        break;
      case AV_SAMPLE_FMT_S32:
      case AV_SAMPLE_FMT_S32P:
      case AV_SAMPLE_FMT_FLT:
      case AV_SAMPLE_FMT_FLTP:
        bytePerOutSample = 4;
        break;
      case AV_SAMPLE_FMT_DBL:
      case AV_SAMPLE_FMT_DBLP:
      case AV_SAMPLE_FMT_S64:
      case AV_SAMPLE_FMT_S64P:
        bytePerOutSample = 8;
        break;
      default:
        bytePerOutSample = 2;
        break;
    }

    int guessOutSamplesPerChannel =
        av_rescale_rnd(inputSamples, out.sampleRate, in.sampleRate, AV_ROUND_UP);
    int guessOutSize = guessOutSamplesPerChannel * out.channels * bytePerOutSample;

    std::cout << "GuessOutSamplesPerChannel: " << guessOutSamplesPerChannel << std::endl;
    std::cout << "GuessOutSize: " << guessOutSize << std::endl;

    guessOutSize *= 1.2;  // just make sure.

    *outData = (uint8_t*)av_malloc(sizeof(uint8_t) * guessOutSize);
    // av_samples_alloc(&outData, NULL, outChannels, guessOutSamplesPerChannel,
    // AV_SAMPLE_FMT_S16, 0);
    return guessOutSize;
  }

  std::tuple<int, int> reSample(uint8_t* dataBuffer, int dataBufferSize,
                                const AVFrame* frame) {
    int outSamples = swr_convert(swr, &dataBuffer, dataBufferSize,
                                 (const uint8_t**)&frame->data[0], frame->nb_samples);
    // cout << "reSample: nb_samples=" << frame->nb_samples << ", sample_rate = " <<
    // frame->sample_rate <<  ", outSamples=" << outSamples << endl;
    if (outSamples <= 0) {
      throw std::runtime_error("error: outSamples=" + outSamples);
    }

    int outDataSize =
        av_samples_get_buffer_size(NULL, out.channels, outSamples, out.format, 1);

    if (outDataSize <= 0) {
      throw std::runtime_error("error: outDataSize=" + outDataSize);
    }

    return {outSamples, outDataSize};
  }
};

}  // namespace ffmpegUtil
