#include "FrameGrabber.h"
#include "EasyWay.h"
#include <iostream>
#include <sstream>

using std::cout;
using std::endl;

/*
 * @return
 *      1: success, a video frame was returned
 *      2: success, a audio frame was returned
 *      0: the decoder has been fully flushed, and there will be no more output frames
 *      negative values: error;
 */
int FrameGrabber::grabFrameByType(AVFrame* pFrame, AVMediaType targetMediaType) {
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
            EasyWay::printDebug("[VIDEO] avcodec_send_packet success.");
            break;
          } else if (ret == AVERROR(EAGAIN)) {
            // buff full, do nothing.
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
            EasyWay::printDebug("[AUDIO] avcodec_send_packet success.");
            break;
          } else if (ret == AVERROR(EAGAIN)) {
            // buff full, do nothing.
          } else {
            string errorMsg = "[AUDIO] avcodec_send_packet error: ";
            errorMsg += ret;
            cout << errorMsg << endl;
            throw std::runtime_error(errorMsg);
          }
        } else {
          stringstream ss{};
          ss << "av_read_frame skip packet in streamIndex=" << currentPacketStreamIndex;
          EasyWay::printDebug(ss.str());
          av_packet_unref(packet);
        }
      } else {
        // file got error or end.
        EasyWay::printDebug("av_read_frame ret < 0");
        fileGotToEnd = true;
        if (vCodecCtx != nullptr) avcodec_send_packet(vCodecCtx, nullptr);
        if (aCodecCtx != nullptr) avcodec_send_packet(aCodecCtx, nullptr);
        break;
      }
    }

    ret = -1;

    if (currentPacketStreamIndex == videoIndex && videoEnabled) {
      ret = avcodec_receive_frame(vCodecCtx, pFrame);
      EasyWay::printDebug("[VIDOE] avcodec_receive_frame !!!");
    } else if (currentPacketStreamIndex == audioIndex && audioEnabled) {
      ret = avcodec_receive_frame(aCodecCtx, pFrame);
      EasyWay::printDebug("[AUDIO] avcodec_receive_frame !!!");
    } else {
      if (fileGotToEnd) {
        cout << "no more frames." << endl;
        return 0;
      } else {
        stringstream ss{};
        ss << "unknown situation: currentPacketStreamIndex:" << currentPacketStreamIndex;
        EasyWay::printDebug(ss.str());
        continue;
      }
    }

    if (ret == 0) {
      if (targetStream == -1 || currentPacketStreamIndex == targetStream) {
        stringstream ss{};
        ss << "avcodec_receive_frame ret == 0. got [" << targetStream << "] ";
        EasyWay::printDebug(ss.str());
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
}

void FrameGrabber::initCodecContext(AVFormatContext* f, int streamIndex, AVCodecContext** ctx) {
  auto codecType = f->streams[streamIndex]->codec->codec_type;

  string codecTypeStr{};
  switch (f->streams[streamIndex]->codec->codec_type) {
    case AVMEDIA_TYPE_VIDEO:
      codecTypeStr = "vidoe_decodec";
      break;
    case AVMEDIA_TYPE_AUDIO:
      codecTypeStr = "audio_decodec";
      break;
    default:
      throw runtime_error("error_decodec, it should not happen.");
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

  cout << codecTypeStr << " [" << codecCtx->codec->name << "] codec context initialize success."
       << endl;
}

FrameGrabber::FrameGrabber(const string& uri, bool enableVideo, bool enableAudio)
    : inputUrl(uri), videoEnabled(enableVideo), audioEnabled(enableAudio) {
  formatCtx = avformat_alloc_context();
}

int FrameGrabber::getWidth() const {
  if (vCodecCtx != nullptr) {
    return vCodecCtx->width;
  } else {
    throw runtime_error("can not getWidth.");
  }
}

int FrameGrabber::getHeight() const {
  if (vCodecCtx != nullptr) {
    return vCodecCtx->height;
  } else {
    throw runtime_error("can not getHeight.");
  }
}

int FrameGrabber::getVideoCodecId() const {
  if (vCodecCtx != nullptr) {
    return static_cast<int>(vCodecCtx->codec_id);
  } else {
    throw runtime_error("can not getVideoCodecId.");
  }
}

string FrameGrabber::getVideoCodecName() const {
  if (vCodecCtx != nullptr) {
    return vCodecCtx->codec->name;
  } else {
    throw runtime_error("can not getVideoCodecName.");
  }
}

int FrameGrabber::getAudioCodecId() const {
  if (aCodecCtx != nullptr) {
    return static_cast<int>(aCodecCtx->codec_id);
  } else {
    throw runtime_error("can not getAudioCodecId.");
  }
}

string FrameGrabber::getAudioCodecName() const {
  if (aCodecCtx != nullptr) {
    return aCodecCtx->codec->name;
  } else {
    throw runtime_error("can not getAudioCodecName.");
  }
}

int FrameGrabber::getPixelFormat() const {
  if (vCodecCtx != nullptr) {
    return static_cast<int>(vCodecCtx->pix_fmt);
  } else {
    throw runtime_error("can not getPixelFormat.");
  }
}

double FrameGrabber::getFrameRate() const {
  if (formatCtx != nullptr) {
    AVRational frame_rate = av_guess_frame_rate(formatCtx, formatCtx->streams[videoIndex], NULL);
    double fr = frame_rate.num && frame_rate.den ? av_q2d(frame_rate) : 0.0;
    return fr;
  } else {
    throw runtime_error("can not getFrameRate.");
  }
}

int FrameGrabber::getChannels() const {
  if (aCodecCtx != nullptr) {
    return aCodecCtx->channels;
  } else {
    throw runtime_error("can not getChannels.");
  }
}

int FrameGrabber::getChannleLayout() const {
  if (aCodecCtx != nullptr) {
    return aCodecCtx->channel_layout;
  } else {
    throw runtime_error("can not getChannleLayout.");
  }
}

int FrameGrabber::getSampleRate() const {
  if (aCodecCtx != nullptr) {
    return aCodecCtx->sample_rate;
  } else {
    throw runtime_error("can not getSampleRate.");
  }
}

int FrameGrabber::getSampleFormat() const {
  if (aCodecCtx != nullptr) {
    return (int)aCodecCtx->sample_fmt;
  } else {
    throw runtime_error("can not getSampleRate.");
  }
}

void FrameGrabber::start() {
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
      initCodecContext(formatCtx, videoIndex, &vCodecCtx);
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
      initCodecContext(formatCtx, audioIndex, &aCodecCtx);
      cout << "Init audio Codec Context" << endl;
    }
  }

  // Output Info-----------------------------
  cout << "--------------- File Information ----------------" << endl;
  av_dump_format(formatCtx, videoIndex, inputUrl.c_str(), 0);
  cout << "-------------------------------------------------\n" << endl;

  packet = (AVPacket*)av_malloc(sizeof(AVPacket));
}

AVCodecContext* FrameGrabber::getAudioContext() const { return aCodecCtx; }

int FrameGrabber::grabImageFrame(AVFrame* pFrame) {
  if (!videoEnabled) {
    throw runtime_error("video disabled.");
  }
  int ret = grabFrameByType(pFrame, AVMediaType::AVMEDIA_TYPE_VIDEO);
  return ret;
}

// int FrameGrabber::grabAudioFrame(AVFrame* pFrame) {
//  if (!audioEnabled) {
//    throw runtime_error("audio disabled.");
//  }
//  int ret = grabFrameByType(pFrame, AVMediaType::AVMEDIA_TYPE_AUDIO);
//  return ret;
//}



int FrameGrabber::grabFrame(AVFrame* pFrame) {
  int ret = grabFrameByType(pFrame, AVMediaType::AVMEDIA_TYPE_UNKNOWN);
  return ret;
}

/*
 * @return
 *      2: success, a audio frame was returned
 *      0: the decoder has been fully flushed, and there will be no more output frames
 *      negative values: error;
 */
int FrameGrabber::grabAudioFrame(AVFrame* pFrame) {

  int ret = -1;

  while (true) {
    ret = avcodec_receive_frame(aCodecCtx, pFrame);
    if (ret == 0) {
      EasyWay::printDebug("avcodec_receive_frame ret == 0");
      return 2;
    } else if (ret == AVERROR(EAGAIN)) {
      EasyWay::printDebug("avcodec_receive_frame ret == AVERROR(EAGAIN)");
      while (true) {
        if (av_read_frame(formatCtx, packet) >= 0) {
          if (packet->stream_index == audioIndex) {
            // feed video packet to codec.
            if (avcodec_send_packet(aCodecCtx, packet) == 0) {
              av_packet_unref(packet);
              EasyWay::printDebug("avcodec_send_packet success.");
              break;
            } else {
              string errorMsg = "avcodec_send_packet error: ";
              errorMsg += ret;
              cout << errorMsg << endl;
              throw std::runtime_error(errorMsg);
            }
          } else {
            // skip Non-video packet.
            EasyWay::printDebug("av_read_frame skip Non-audio packet.");
            av_packet_unref(packet);
          }
        } else {
          // file got error or end.
          EasyWay::printDebug("av_read_frame ret < 0");
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
/*
 * @return
 *      0: success, a frame was returned
 *      1: the decoder has been fully flushed, and there will be no more output frames
 *      negative values: error;
 */
int FrameGrabber::grabImageFrame_bkp(AVFrame* pFrame) {
  int got_picture = 0;

  int ret = -1;

  while (true) {
    ret = avcodec_receive_frame(vCodecCtx, pFrame);
    if (ret == 0) {
      EasyWay::printDebug("avcodec_receive_frame ret == 0");
      return 0;
    } else if (ret == AVERROR(EAGAIN)) {
      EasyWay::printDebug("avcodec_receive_frame ret == AVERROR(EAGAIN)");
      while (true) {
        if (av_read_frame(formatCtx, packet) >= 0) {
          if (packet->stream_index == videoIndex) {
            // feed video packet to codec.
            if (avcodec_send_packet(vCodecCtx, packet) == 0) {
              av_packet_unref(packet);
              EasyWay::printDebug("avcodec_send_packet success.");
              break;
            } else {
              string errorMsg = "avcodec_send_packet error: ";
              errorMsg += ret;
              cout << errorMsg << endl;
              throw std::runtime_error(errorMsg);
            }
          } else {
            // skip Non-video packet.
            EasyWay::printDebug("av_read_frame skip Non-video packet.");
            av_packet_unref(packet);
          }
        } else {
          // file got error or end.
          EasyWay::printDebug("av_read_frame ret < 0");
          avcodec_send_packet(vCodecCtx, nullptr);
          break;
        }
      }

    } else if (ret == AVERROR_EOF) {
      cout << "no more output frames." << endl;
      return 1;
    } else {
      string errorMsg = "avcodec_receive_frame error: ";
      errorMsg += ret;
      cout << errorMsg << endl;
      throw std::runtime_error(errorMsg);
    }
  }
}

void FrameGrabber::close() {
  // It seems like only one xxx_free_context can be called.
  // Which one should be called?
  avformat_free_context(formatCtx);
  // avcodec_free_context(&pCodecCtx);
  // TODO implement.
}
