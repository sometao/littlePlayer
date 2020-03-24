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
      errorMsg += (*ctx)->codec_id;
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

    av_dump_format(f, streamIndex, "", 0);

  }
};

class PacketGrabber {
  const string inputUrl;
  AVFormatContext* formatCtx = nullptr;
  bool fileGotToEnd = false;

 public:
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

  bool isFileEnd() const {return fileGotToEnd; }

};



class FrameGrabber {
  const string inputUrl;

  const bool videoEnabled;
  const bool audioEnabled;

  int videoCodecId = -1;
  int audioCodecId = -1;

  int videoIndex = -1;
  int audioIndex = -1;

  string videoCondecName = "Unknown";
  AVFormatContext* formatCtx = nullptr;
  AVCodecContext* vCodecCtx = nullptr;
  AVCodecContext* aCodecCtx = nullptr;
  AVPacket* packet = (AVPacket*)av_malloc(sizeof(AVPacket));
  bool fileGotToEnd = false;

  /*
   * @return
   *      1: success, a video frame was returned
   *      2: success, a audio frame was returned
   *      0: the decoder has been fully flushed, and there will be no more output frames
   *      negative values: error;
   */
  int grabFrameByType(AVFrame* pFrame, AVMediaType targetMediaType) {
    int ret = -1;
    int targetStream;
    switch (targetMediaType) {
      case AVMEDIA_TYPE_VIDEO:
        targetStream = videoIndex;
        break;
      case AVMEDIA_TYPE_AUDIO:
        targetStream = audioIndex;
        break;
      default:
        targetStream = -1;
        break;
    }

    while (true) {
      int currentPacketStreamIndex = -1;
      while (!fileGotToEnd) {
        if (av_read_frame(formatCtx, packet) >= 0) {
          currentPacketStreamIndex = packet->stream_index;
          if (packet->stream_index == videoIndex && videoEnabled) {
            // feed video packet to codec.

            ret = avcodec_send_packet(vCodecCtx, packet);
            if (ret == 0) {
              av_packet_unref(packet);
              // cout << "[VIDEO] avcodec_send_packet success." << endl;;
              break;
            } else if (ret == AVERROR(EAGAIN)) {
              // buff full, can not decode anymore, do nothing.
            } else {
              string errorMsg = "[VIDEO] avcodec_send_packet error: ";
              errorMsg += ret;
              cout << errorMsg << endl;
              throw std::runtime_error(errorMsg);
            }
          } else if (packet->stream_index == audioIndex && audioEnabled) {
            // feed audio packet to codec.
            ret = avcodec_send_packet(aCodecCtx, packet);
            if (ret == 0) {
              av_packet_unref(packet);
              // cout << "[AUDIO] avcodec_send_packet success." << endl;;
              break;
            } else if (ret == AVERROR(EAGAIN)) {
              // buff full, can not decode anymore, do nothing.
            } else {
              string errorMsg = "[AUDIO] avcodec_send_packet error: ";
              errorMsg += ret;
              cout << errorMsg << endl;
              throw std::runtime_error(errorMsg);
            }
          } else {
            stringstream ss{};
            ss << "av_read_frame skip packet in streamIndex=" << currentPacketStreamIndex;
            // cout << ss.str()) << endl;;
            av_packet_unref(packet);
          }
        } else {
          // file got error or end.
          // cout << "av_read_frame ret < 0" << endl;
          fileGotToEnd = true;
          if (vCodecCtx != nullptr) avcodec_send_packet(vCodecCtx, nullptr);
          if (aCodecCtx != nullptr) avcodec_send_packet(aCodecCtx, nullptr);
          break;
        }
      }

      ret = -1;

      if (currentPacketStreamIndex == videoIndex && videoEnabled) {
        ret = avcodec_receive_frame(vCodecCtx, pFrame);
        // cout <<  "[VIDOE] avcodec_receive_frame !!!" << endl;;
      } else if (currentPacketStreamIndex == audioIndex && audioEnabled) {
        ret = avcodec_receive_frame(aCodecCtx, pFrame);
        // cout <<  "[AUDIO] avcodec_receive_frame !!!" << endl;
      } else {
        if (fileGotToEnd) {
          cout << "no more frames." << endl;
          return 0;
        } else {
          stringstream ss{};
          ss << "unknown situation: currentPacketStreamIndex:" << currentPacketStreamIndex;
          // cout << ss.str() << endl;
          continue;
        }
      }

      if (ret == 0) {
        if (targetStream == -1 || currentPacketStreamIndex == targetStream) {
          stringstream ss{};
          ss << "avcodec_receive_frame ret == 0. got [" << targetStream << "] ";
          // cout <<  ss.str() << endl;
          if (currentPacketStreamIndex == videoIndex) {
            return 1;
          } else if (currentPacketStreamIndex == audioIndex) {
            return 2;
          } else {
            string errorMsg = "Unknown situation, it should never happen. ";
            errorMsg += ret;
            cout << errorMsg << endl;
            throw std::runtime_error(errorMsg);
          }
        } else {
          // the got frame is not target, try a again.
          continue;
        }
      } else if (ret == AVERROR_EOF) {
        cout << "no more output frames." << endl;
        return 0;
      } else if (ret == AVERROR(EAGAIN)) {
        // string errorMsg = "avcodec_receive_frame EAGAIN, it should never happen. ";
        // errorMsg += ret;
        // cout << errorMsg << endl;
        // throw std::runtime_error(errorMsg);
        continue;
      } else {
        string errorMsg = "avcodec_receive_frame error: ";
        errorMsg += ret;
        cout << errorMsg << endl;
        throw std::runtime_error(errorMsg);
      }
    }
  };

 public:
  FrameGrabber(const string& uri, bool enableVideo = true, bool enableAudio = true)
      : inputUrl(uri), videoEnabled(enableVideo), audioEnabled(enableAudio) {
    formatCtx = avformat_alloc_context();
  }

  int getWidth() const {
    if (vCodecCtx != nullptr) {
      return vCodecCtx->width;
    } else {
      throw std::runtime_error("can not getWidth.");
    }
  }

  int getHeight() const {
    if (vCodecCtx != nullptr) {
      return vCodecCtx->height;
    } else {
      throw std::runtime_error("can not getHeight.");
    }
  }

  int getVideoCodecId() const {
    if (vCodecCtx != nullptr) {
      return static_cast<int>(vCodecCtx->codec_id);
    } else {
      throw std::runtime_error("can not getVideoCodecId.");
    }
  }

  string getVideoCodecName() const {
    if (vCodecCtx != nullptr) {
      return vCodecCtx->codec->name;
    } else {
      throw std::runtime_error("can not getVideoCodecName.");
    }
  }

  int getAudioCodecId() const {
    if (aCodecCtx != nullptr) {
      return static_cast<int>(aCodecCtx->codec_id);
    } else {
      throw std::runtime_error("can not getAudioCodecId.");
    }
  }

  string getAudioCodecName() const {
    if (aCodecCtx != nullptr) {
      return aCodecCtx->codec->name;
    } else {
      throw std::runtime_error("can not getAudioCodecName.");
    }
  }

  int getPixelFormat() const {
    if (vCodecCtx != nullptr) {
      return static_cast<int>(vCodecCtx->pix_fmt);
    } else {
      throw std::runtime_error("can not getPixelFormat.");
    }
  }

  double getFrameRate() const {
    if (formatCtx != nullptr) {
      AVRational frame_rate =
          av_guess_frame_rate(formatCtx, formatCtx->streams[videoIndex], NULL);
      double fr = frame_rate.num && frame_rate.den ? av_q2d(frame_rate) : 0.0;
      return fr;
    } else {
      throw std::runtime_error("can not getFrameRate.");
    }
  }

  int getChannels() const {
    if (aCodecCtx != nullptr) {
      return aCodecCtx->channels;
    } else {
      throw std::runtime_error("can not getChannels.");
    }
  }

  int getChannleLayout() const {
    if (aCodecCtx != nullptr) {
      return aCodecCtx->channel_layout;
    } else {
      throw std::runtime_error("can not getChannleLayout.");
    }
  }

  int getSampleRate() const {
    if (aCodecCtx != nullptr) {
      return aCodecCtx->sample_rate;
    } else {
      throw std::runtime_error("can not getSampleRate.");
    }
  }

  int getSampleFormat() const {
    if (aCodecCtx != nullptr) {
      return (int)aCodecCtx->sample_fmt;
    } else {
      throw std::runtime_error("can not getSampleRate.");
    }
  }

  void start() {
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

    if (videoEnabled) {
      if (videoIndex == -1) {
        string errorMsg = "Do not find a video stream in file: ";
        errorMsg += inputUrl;
        cout << errorMsg << endl;
        throw std::runtime_error(errorMsg);
      } else {
        ffUtils::initCodecContext(formatCtx, videoIndex, &vCodecCtx);
        cout << "Init video Codec Context" << endl;
      }
    }

    if (audioEnabled) {
      if (audioIndex == -1) {
        string errorMsg = "Do not find a audio stream in file: ";
        errorMsg += inputUrl;
        cout << errorMsg << endl;
        throw std::runtime_error(errorMsg);
      } else {
        ffUtils::initCodecContext(formatCtx, audioIndex, &aCodecCtx);
        cout << "Init audio Codec Context" << endl;
      }
    }

    // Output Info-----------------------------
    cout << "--------------- File Information ----------------" << endl;
    av_dump_format(formatCtx, videoIndex, inputUrl.c_str(), 0);
    cout << "-------------------------------------------------\n" << endl;

    packet = (AVPacket*)av_malloc(sizeof(AVPacket));
  }

  AVCodecContext* getAudioContext() const { return aCodecCtx; }

  int grabImageFrame(AVFrame* pFrame) {
    if (!videoEnabled) {
      throw std::runtime_error("video disabled.");
    }
    int ret = grabFrameByType(pFrame, AVMediaType::AVMEDIA_TYPE_VIDEO);
    return ret;
  }

  int grabAudioFrame(AVFrame* pFrame) {
    if (!audioEnabled) {
      throw std::runtime_error("audio disabled.");
    }
    int ret = grabFrameByType(pFrame, AVMediaType::AVMEDIA_TYPE_AUDIO);
    return ret;
  }

  int grabFrame(AVFrame* pFrame) {
    int ret = grabFrameByType(pFrame, AVMediaType::AVMEDIA_TYPE_UNKNOWN);
    return ret;
  }

  /*
   * @return
   *      2: success, a audio frame was returned
   *      0: the decoder has been fully flushed, and there will be no more output frames
   *      negative values: error;
   */
  int grabAudioFrame_bkp(AVFrame* pFrame) {
    int ret = -1;

    while (true) {
      ret = avcodec_receive_frame(aCodecCtx, pFrame);
      if (ret == 0) {
        // cout << "avcodec_receive_frame ret == 0" << endl;
        return 2;
      } else if (ret == AVERROR(EAGAIN)) {
        // cout << "avcodec_receive_frame ret == AVERROR(EAGAIN)" << endl;;
        while (true) {
          if (av_read_frame(formatCtx, packet) >= 0) {
            if (packet->stream_index == audioIndex) {
              // feed video packet to codec.
              if (avcodec_send_packet(aCodecCtx, packet) == 0) {
                av_packet_unref(packet);
                // cout << ("avcodec_send_packet success.";
                break;
              } else {
                string errorMsg = "avcodec_send_packet error: ";
                errorMsg += ret;
                cout << errorMsg << endl;
                throw std::runtime_error(errorMsg);
              }
            } else {
              // skip Non-video packet.
              // cout << "av_read_frame skip Non-audio packet.");
              av_packet_unref(packet);
            }
          } else {
            // file got error or end.
            // cout << "av_read_frame ret < 0" << endl;
            avcodec_send_packet(aCodecCtx, nullptr);
            break;
          }
        }

      } else if (ret == AVERROR_EOF) {
        cout << "no more output frames." << endl;
        return 0;
      } else {
        string errorMsg = "avcodec_receive_frame error: ";
        errorMsg += ret;
        cout << errorMsg << endl;
        throw std::runtime_error(errorMsg);
      }
    }
  }

  /*
   * @return
   *      1: success, a frame was returned
   *      0: the decoder has been fully flushed, and there will be no more output frames
   *      negative values: error;
   */
  int grabImageFrame_bkp(AVFrame* pFrame) {
    cout << "grabImageFrame" << endl;
    int got_picture = 0;

    int ret = -1;

    while (true) {
      ret = avcodec_receive_frame(vCodecCtx, pFrame);
      if (ret == 0) {
        // cout << "avcodec_receive_frame ret == 0" << endl;
        return 1;
      } else if (ret == AVERROR(EAGAIN)) {
        // cout << "avcodec_receive_frame ret == AVERROR(EAGAIN)" << endl;
        while (true) {
          if (av_read_frame(formatCtx, packet) >= 0) {
            if (packet->stream_index == videoIndex) {
              // feed video packet to codec.
              if (avcodec_send_packet(vCodecCtx, packet) == 0) {
                av_packet_unref(packet);
                // cout << "avcodec_send_packet success." << endl;
                break;
              } else {
                string errorMsg = "avcodec_send_packet error: ";
                errorMsg += ret;
                cout << errorMsg << endl;
                throw std::runtime_error(errorMsg);
              }
            } else {
              // skip Non-video packet.
              // cout << "av_read_frame skip Non-video packet."<< endl;
              av_packet_unref(packet);
            }
          } else {
            // file got error or end.
            // cout << "av_read_frame ret < 0"<< endl;
            avcodec_send_packet(vCodecCtx, nullptr);
            break;
          }
        }

      } else if (ret == AVERROR_EOF) {
        cout << "no more output frames." << endl;
        return 0;
      } else {
        string errorMsg = "avcodec_receive_frame error: ";
        errorMsg += ret;
        cout << errorMsg << endl;
        throw std::runtime_error(errorMsg);
      }
    }
  }

  void close() {
    // It seems like only one xxx_free_context can be called.
    // Which one should be called?
    avformat_free_context(formatCtx);
    // avcodec_free_context(&pCodecCtx);
    // TODO implement.
  }
};

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

  int reSample(uint8_t* dataBuffer, int dataBufferSize, const AVFrame* frame) {
    int outSamples = swr_convert(swr, &dataBuffer, dataBufferSize,
                                 (const uint8_t**)&frame->data[0], frame->nb_samples);

    if (outSamples <= 0) {
      throw std::runtime_error("error: outSamples=" + outSamples);
    }

    int outDataSize =
        av_samples_get_buffer_size(NULL, out.channels, outSamples, out.format, 1);

    if (outDataSize <= 0) {
      throw std::runtime_error("error: outDataSize=" + outDataSize);
    }

    return outDataSize;
  }
};

}  // namespace ffmpegUtil
