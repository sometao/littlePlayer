
#include <iostream>
#include <fstream>
//#include "FrameGrabber.h"
#include "ffmpegUtil.h"


using std::cout;
using std::endl;
using std::string;

namespace ffmpegUtil {
extern void writeY420pFrame(std::ofstream& os, AVFrame* frame);

}

extern void playVideo(const string& inputPath);
extern void playAudioBySDL(const string& inputPath);
extern void playAudioByOpenAL(const string& inputPath);

void testReadFileInfo() {
  using namespace ffmpegUtil;

  string inputFile = "D:/data/video/VID_20181123_214724.mp4";
  string outputFile = "tempout001.yuv";
  FrameGrabber grabber{inputFile};

  try {
    grabber.start();

    cout << "getWidth:" << grabber.getWidth() << endl;
    cout << "getHeight:" << grabber.getHeight() << endl;
    cout << "getFrameRate:" << grabber.getFrameRate() << endl;
    cout << "getPixelFormat:" << grabber.getPixelFormat() << endl;
    cout << "getVideoCodecId:" << grabber.getVideoCodecId() << endl;
    cout << "getVideoCodecName:" << grabber.getVideoCodecName() << endl;

    cout << "------------------------------------------------" << endl;
    AVFrame* frame = av_frame_alloc();

    int count = 0;

    std::ofstream os{outputFile, std::ios::binary};

    int height = grabber.getHeight();
    int width = grabber.getWidth();
    cout << "height: " << height << endl;

    while (grabber.grabImageFrame(frame) == 1) {
      ffmpegUtil::writeY420pFrame(os, frame);
      count += 1;
      if (count % 10 == 0) {
        cout << "image frame count:" << count << endl;
      }
    }
    os.close();
    cout << "------------------" << endl;
    cout << "image frame count:" << count << endl;
  } catch (std::exception ex) {
    cout << "got exception:" << ex.what() << endl;
  }

  cout << "DONE." << endl;
}


void testPlayVideo() {
  //const char* inputPath = "D:/data/video/VID_20181123_214724.mp4";
  string inputPath = "D:/data/video/v1_out10.mp4";
  // string inputPath = "D:/data/video/p3_out1.mp4";
  //const string inputPath = "D:/media/Music/test/MyLove.mp3";
  //string inputPath = "D:/data/tmp/ffmepg_test/output002.yuv";
  playVideo(inputPath);

}



void testPlayAudio() {

  cout << "hello, audio." << endl;
  string inputPath = "D:/media/Music/test/MyLove.mp3";

  //playAudioBySDL(inputPath);
  playAudioByOpenAL(inputPath);
}

int main(int argc, char* argv[]) {
  cout << "hello, little player." << endl;

  //testReadFileInfo();
  //testPlayVideo();

  testPlayAudio();

  return 0;
}