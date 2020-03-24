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

void sdlAudioCallback(void* userdata, Uint8* stream, int len) {
  AudioProcessor* receiver = (AudioProcessor*)userdata;
  receiver->writeAudioData(stream, len);
}

void pktReader(PacketGrabber& pGrabber, AudioProcessor& aProcessor) {
  const int CHECK_PERIOD = 15;

  cout << "INFO: pkt Reader thread started." << endl;
  auto audioIndex = aProcessor.getAudioIndex();

  while (!pGrabber.isFileEnd()) {
    if (aProcessor.needPacket()) {
      PacketDeleter d{};
      AVPacket* packet = (AVPacket*)av_malloc(sizeof(AVPacket));
      while (true) {
        int t = pGrabber.grabPacket(packet);
        // UNDONE only get audio pkt for test.
        if (t == audioIndex) {
          unique_ptr<AVPacket, PacketDeleter> uPacket(packet, d);
          aProcessor.pushPkt(std::move(uPacket));
          break;
        }
      }
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(CHECK_PERIOD));
    }
  }
  cout << "INFO: pkt Reader thread finished." << endl;
}

void playVideo() {
  //TODO here.
}

void playAudio(AudioProcessor& aProcessor) {
  //--------------------- GET SDL audio READY -------------------

  // audio specs containers
  SDL_AudioSpec wanted_specs;
  SDL_AudioSpec specs;

  cout << "grabber.getSampleFormat() = " << aProcessor.getSampleFormat() << endl;
  cout << "grabber.getSampleRate() = " << aProcessor.getSampleRate() << endl;
  cout << "++" << endl;

  // set audio settings from codec info
  wanted_specs.freq = aProcessor.getSampleRate();
  wanted_specs.format = AUDIO_S16SYS;
  wanted_specs.channels = aProcessor.getChannels();
  wanted_specs.samples = 1152;
  wanted_specs.callback = sdlAudioCallback;
  wanted_specs.userdata = &aProcessor;

  // Uint32 audio device id
  SDL_AudioDeviceID audioDeviceID;

  // open audio device
  audioDeviceID = SDL_OpenAudioDevice(  // [1]
    nullptr, 0, &wanted_specs, &specs, 0);

  // SDL_OpenAudioDevice returns a valid device ID that is > 0 on success or 0 on failure
  if (audioDeviceID == 0) {
    string errMsg = "Failed to open audio device:";
    errMsg += SDL_GetError();
    cout << errMsg << endl;
    throw std::runtime_error(errMsg);
  }

  cout << "wanted_specs.freq:" << wanted_specs.freq << endl;
  // cout << "wanted_specs.format:" << wanted_specs.format << endl;
  std::printf("wanted_specs.format: Ox%X\n", wanted_specs.format);
  cout << "wanted_specs.channels:" << (int)wanted_specs.channels << endl;
  cout << "wanted_specs.samples:" << (int)wanted_specs.samples << endl;

  cout << "------------------------------------------------" << endl;

  cout << "specs.freq:" << specs.freq << endl;
  // cout << "specs.format:" << specs.format << endl;
  std::printf("specs.format: Ox%X\n", specs.format);
  cout << "specs.channels:" << (int)specs.channels << endl;
  cout << "specs.silence:" << (int)specs.silence << endl;
  cout << "specs.samples:" << (int)specs.samples << endl;

  cout << "waiting audio play..." << endl;

  SDL_PauseAudioDevice(audioDeviceID, 0);  // [2]


}


int play(const string& inputFile) {
  // create packet grabber
  PacketGrabber packetGrabber{inputFile};
  auto formatCtx = packetGrabber.getFormatCtx();

  // create AudioProcessor
  AudioProcessor audioProcessor(formatCtx);
  auto audioIndex = audioProcessor.getAudioIndex();
  audioProcessor.start();

  cout << "create AudioProcessor done." << endl;

  // start pkt reader
  std::thread readerThread{pktReader, std::ref(packetGrabber), std::ref(audioProcessor)};

  SDL_setenv("SDL_AUDIO_ALSA_SET_BUFFER_SIZE", "1", 1);

  if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
    string errMsg = "Could not initialize SDL -";
    errMsg += SDL_GetError();
    cout << errMsg << endl;
    throw std::runtime_error(errMsg);
  }

  std::thread playAudioThread{ playAudio, std::ref(audioProcessor) };
  playAudioThread.detach();



  SDL_Delay(30000);
  readerThread.join();




  SDL_CloseAudio();

  return 0;
}

}  // namespace

void playVideoWithAudio(const string& inputFile) {
  std::cout << "playVideoWithAudio: " << inputFile << std::endl;

  play(inputFile);
}