#include "ffmpegUtil.h"

#include <iostream>
#include <string>
#include <list>
#include <memory>
#include <chrono>
#include <thread>
#include "MediaProcessor.hpp"

extern "C" {
#include "SDL2/SDL.h"
};

namespace {

using namespace ffmpegUtil;

using std::cout;
using std::endl;

int play(const string& inputFile) {
  // TODO
  // create packet grabber
  PacketGrabber packetGrabber{inputFile};
  auto formatCtx = packetGrabber.getFormatCtx();

  // create AudioProcessor
  AudioProcessor audioProcessor(formatCtx);
  auto audioIndex = audioProcessor.getAudioIndex();

  while (!packetGrabber.isFileEnd()) {
    PacketDeleter d{};
    AVPacket* packet = (AVPacket*)av_malloc(sizeof(AVPacket));
    int t = packetGrabber.grabPacket(packet);
    if (t == audioIndex) {
      unique_ptr<AVPacket, PacketDeleter> uPacket(packet, d);
      audioProcessor.pushPkt(std::move(uPacket));
    } else {
      //skip
    }

    //TODO continue here.


  }

  // get packet

  return 0;
}

}  // namespace

void playVideoWithAudio(const string& inputFile) {
  std::cout << "playVideoWithAudio: " << inputFile << std::endl;

  play(inputFile);
}