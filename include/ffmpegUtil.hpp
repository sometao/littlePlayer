#pragma once

#include <string>
#include <iostream>

extern "C" {
#include <libswresample/swresample.h>
}

namespace ffmpegUtils {

struct AudioInfo {
  int64_t layout;
  int sampleRate;
  int channels;
  AVSampleFormat format;

  AudioInfo(int64_t l, int rate, int c, AVSampleFormat f)
      : layout(l), sampleRate(rate), channels(c), format(f) {}
};

class ReSampler {
  SwrContext* swr;

 public:
  const AudioInfo in;
  const AudioInfo out;

  static AudioInfo getDefaultAudioInfo() {
    int64_t layout = AV_CH_LAYOUT_STEREO;
    int sampleRate = 44100;
    int channels = 2;
    AVSampleFormat format = AV_SAMPLE_FMT_S16;

    return ffmpegUtils::AudioInfo(layout, sampleRate, channels, format);
  }

  ReSampler(AudioInfo input, AudioInfo output) : in(input), out(output) {
    swr = swr_alloc_set_opts(nullptr, out.layout, out.format, out.sampleRate, in.layout, in.format,
                             in.sampleRate, 0, nullptr);

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
    // av_samples_alloc(&outData, NULL, outChannels, guessOutSamplesPerChannel, AV_SAMPLE_FMT_S16,
    // 0);
    return guessOutSize;
  }

  int reSample(uint8_t* dataBuffer, int dataBufferSize, const AVFrame* frame) {
    int outSamples = swr_convert(swr, &dataBuffer, dataBufferSize, (const uint8_t**)&frame->data[0],
                                 frame->nb_samples);

    if (outSamples <= 0) {
      throw std::runtime_error("error: outSamples=" + outSamples);
    }

    int outDataSize = av_samples_get_buffer_size(NULL, out.channels, outSamples, out.format, 1);

    if (outDataSize <= 0) {
      throw std::runtime_error("error: outDataSize=" + outDataSize);
    }

    return outDataSize;
  }

};

}  // namespace ffmpegUtils
