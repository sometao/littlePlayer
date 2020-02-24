#include "FrameGrabber.h"

#include <iostream>

#include "constants.h"

using std::cout;
using std::endl;

FrameGrabber::FrameGrabber(const string& uri) : inputUrl(uri) {
  pFormatCtx = avformat_alloc_context();
}

int FrameGrabber::start() {
  if (avformat_open_input(&pFormatCtx, inputUrl.c_str(), NULL, NULL) != 0) {
    cout << "Can not open input file:" << inputUrl << endl;
    return -1;
  }

  if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
    cout << "Can not find stream information in input file:" << inputUrl << endl;
    return -1;
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
    cout << "Do not find a video stream." << endl;
    ;
    return -1;
  }

  pCodecCtx = pFormatCtx->streams[videoIndex]->codec;
  AVCodec* pCodec = avcodec_find_decoder(pCodecCtx->codec_id);

  if (pCodec == nullptr) {
    cout << "Codec[" << pCodecCtx->codec_id << "] not found." << endl;
    return -1;
  }

  if (avcodec_open2(pCodecCtx, pCodec, nullptr) < 0) {
    cout << "Could not open codec." << endl;
    ;
    return -1;
  }

  cout << "[" << pCodecCtx->codec->name << "] codec context initialize success." << endl;

  return 0;
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
    return pCodecCtx->codec_id;
  } else {
    return -1;
  }
}

const string FrameGrabber::getVideoCodecName() const {
  if (pCodecCtx != nullptr) {
    return pCodecCtx->codec->name;
  } else {
    return "unknown";
  }
}

int FrameGrabber::grab(AVFrame* frame) {

  //TODO implement.
  return 0;
}

void FrameGrabber::close() {
  //TODO implement.
}
