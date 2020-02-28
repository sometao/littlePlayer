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
  int grabFrameByType(AVFrame* pFrame, AVMediaType mediaType);


  static void initCodecContext(AVFormatContext* f, int streamIndex, AVCodecContext** ctx);


 public:
  FrameGrabber(const string& uri, bool enableVideo = true, bool enableAudio = true);
  void start();
  int getWidth() const;
  int getHeight() const;
  int getVideoCodecId() const;
  string getVideoCodecName()  const;
  int getPixelFormat()  const;
  double getFrameRate();
  int grabAudioFrame(AVFrame* pFrame);
  int grabFrame(AVFrame* pFrame);
  int grabImageFrame(AVFrame* pFrame);
  int grabImageFrame_bkp(AVFrame* pFrame);
  void close();
};