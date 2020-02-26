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
#include <iostream>

#include <string>
#include <fstream>

using std::string;

namespace ffmpegUtil {

void writeY420pData(std::ofstream& os, unsigned char* data, int linesize, int width,
                    int height) {
  char* p = reinterpret_cast<char*>(data);
  for (int i = 0; i < height; i++) {
    os.write(p + (__int64)i * linesize, width);
  }
}

void writeY420pFrame(std::ofstream& os, AVFrame* frame) {
  int width = frame->width;
  int height = frame->height;
  writeY420pData(os, frame->data[0], frame->linesize[0], width, height);
  writeY420pData(os, frame->data[1], frame->linesize[1], width / 2, height / 2);
  writeY420pData(os, frame->data[2], frame->linesize[2], width / 2, height / 2);
}

}  // namespace ffmpegUtil