#pragma once

#ifdef _WIN32
// Windows
extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libavutil/imgutils.h"
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
#ifdef __cplusplus
};
#endif
#endif

#include <string>

using std::string;

class FrameGrabber {
  
  const string inputUrl;
  int videoCodecId = -1;
  int videoIndex = -1;
  string videoCondecName = "Unknown";
  AVFormatContext* pFormatCtx = nullptr;
  AVCodecContext* pCodecCtx = nullptr;



 public:
  FrameGrabber(const string& uri);
  int start();
  int getWidth() const;
  int getHeight() const;
  int getVideoCodecId() const;
  const string getVideoCodecName()  const;
  double getFrameRate();
  int grab(AVFrame* frame);
  void close();
};