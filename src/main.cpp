#include <iostream>
#include <fstream>
#include "ffmpegUtil.h"

using std::cout;
using std::endl;
using std::string;

namespace ffmpegUtil {
extern void writeY420pFrame(std::ofstream& os, AVFrame* frame);
}

extern void playVideoWithAudio(const string& inputPath);

int main(int argc, char* argv[]) {
  cout << "hello, little player." << endl;
  if (argc != 2) {
    cout << "input error:" << endl;
    cout << "arg[1] should be the media file." << endl;
  } else {
    string inputPath = argv[1];
    cout << "play file:" << inputPath << endl;
    playVideoWithAudio(inputPath);
  }
  return 0;
}