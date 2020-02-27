#include "FrameGrabber.h"
#include "EasyWay.h"
#include <iostream>
#include <sstream>

using std::cout;
using std::endl;

FrameGrabber::FrameGrabber(const string& uri) : inputUrl(uri) {
  pFormatCtx = avformat_alloc_context();
}

int FrameGrabber::getWidth() const {
  if (pCodecCtx != nullptr) {
    return pCodecCtx->width;
  } else {
    return -1;
  }
}

int FrameGrabber::getHeight() const {
  if (pCodecCtx != nullptr) {
    return pCodecCtx->height;
  } else {
    return -1;
  }
}

int FrameGrabber::getVideoCodecId() const {
  if (pCodecCtx != nullptr) {
    return static_cast<int>(pCodecCtx->codec_id);
  } else {
    return -1;
  }
}

string FrameGrabber::getVideoCodecName() const {
  if (pCodecCtx != nullptr) {
    return pCodecCtx->codec->name;
  } else {
    return "unknown";
  }
}

int FrameGrabber::getPixelFormat() const {
  if (pCodecCtx != nullptr) {
    return static_cast<int>(pCodecCtx->pix_fmt);
  } else {
    return -1;
  }
}

double FrameGrabber::getFrameRate() {
  if (pCodecCtx != nullptr) {
    double numerator = pCodecCtx->framerate.num;
    double denominator = pCodecCtx->framerate.den;
    return numerator / denominator;
  } else {
    return -1.0;
  }
}

void FrameGrabber::start() {
  if (avformat_open_input(&pFormatCtx, inputUrl.c_str(), NULL, NULL) != 0) {
    string errorMsg = "Can not open input file:";
    errorMsg += inputUrl;
    cout << errorMsg << endl;
    throw std::runtime_error(errorMsg);
  }

  if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
    string errorMsg = "Can not find stream information in input file:";
    errorMsg += inputUrl;
    cout << errorMsg << endl;
    throw std::runtime_error(errorMsg);
  }

  for (int i = 0; i < pFormatCtx->nb_streams; i++) {
    if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
      videoIndex = i;
      cout << "file video stream: [" << i << "] type: ["
           << pFormatCtx->streams[i]->codec->codec_type << "]" << endl;
      break;
    }
  }

  if (videoIndex == -1) {
    string errorMsg = "Do not find a video stream in file: ";
    errorMsg += inputUrl;
    cout << errorMsg << endl;
    throw std::runtime_error(errorMsg);
  }

  AVCodec* pCodec = avcodec_find_decoder(pCodecCtx->codec_id);

  if (pCodec == nullptr) {
    string errorMsg = "Could not find codec: ";
    errorMsg += pCodecCtx->codec_id;
    cout << errorMsg << endl;
    throw std::runtime_error(errorMsg);
  }

  pCodecCtx = avcodec_alloc_context3(pCodec);

  if (avcodec_parameters_to_context(pCodecCtx, pFormatCtx->streams[videoIndex]->codecpar) != 0) {
    string errorMsg = "Could not copy codec context: ";
    errorMsg += pCodec->name;
    cout << errorMsg << endl;
    throw std::runtime_error(errorMsg);
  }

  if (avcodec_open2(pCodecCtx, pCodec, nullptr) < 0) {
    string errorMsg = "Could not open codec: ";
    errorMsg += pCodec->name;
    cout << errorMsg << endl;
    throw std::runtime_error(errorMsg);
  }

  cout << "[" << pCodecCtx->codec->name << "] codec context initialize success." << endl;

  // Output Info-----------------------------
  cout << "--------------- File Information ----------------" << endl;
  av_dump_format(pFormatCtx, videoIndex, inputUrl.c_str(), 0);
  cout << "-------------------------------------------------\n" << endl;

  packet = (AVPacket*)av_malloc(sizeof(AVPacket));
}



/*
 * @return
 *      0: success, a frame was returned
 *      1: the decoder has been fully flushed, and there will be no more output frames
 *      negative values: error;
 */
int FrameGrabber::grabImageFrame(AVFrame* pFrame) {
  int ret = -1;

  EasyWay::printDebug("avcodec_receive_frame ret == AVERROR(EAGAIN)");
  while (true) {
    if (!fileGotToEnd && av_read_frame(pFormatCtx, packet) >= 0) {
      if (packet->stream_index == videoIndex) {
        // feed video packet to codec.
        ret = avcodec_send_packet(pCodecCtx, packet);
        if (ret == 0) {
          av_packet_unref(packet);
          EasyWay::printDebug("avcodec_send_packet success.");
          break;
        } else if (ret == AVERROR(EAGAIN)) {
          // buff full, do nothing.
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
      fileGotToEnd = true;
      avcodec_send_packet(pCodecCtx, nullptr);
      break;
    }
  }

  ret = avcodec_receive_frame(pCodecCtx, pFrame);
  if (ret == 0) {
    EasyWay::printDebug("avcodec_receive_frame ret == 0");
    return 0;
  }  else if (ret == AVERROR_EOF) {
    cout << "no more output frames." << endl;
    return 1;
  } else if (ret == AVERROR(EAGAIN)) {
    string errorMsg = "avcodec_receive_frame EAGAIN, it should never happen. ";
    errorMsg += ret;
    cout << errorMsg << endl;
    throw std::runtime_error(errorMsg);
  } else {
    string errorMsg = "avcodec_receive_frame error: ";
    errorMsg += ret;
    cout << errorMsg << endl;
    throw std::runtime_error(errorMsg);
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
    ret = avcodec_receive_frame(pCodecCtx, pFrame);
    if (ret == 0) {
      EasyWay::printDebug("avcodec_receive_frame ret == 0");
      return 0;
    } else if (ret == AVERROR(EAGAIN)) {
      EasyWay::printDebug("avcodec_receive_frame ret == AVERROR(EAGAIN)");
      while (true) {
        if (av_read_frame(pFormatCtx, packet) >= 0) {
          if (packet->stream_index == videoIndex) {
            // feed video packet to codec.
            if (avcodec_send_packet(pCodecCtx, packet) == 0) {
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
          avcodec_send_packet(pCodecCtx, nullptr);
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
  avformat_free_context(pFormatCtx);
  //avcodec_free_context(&pCodecCtx);


  // TODO implement.
}
