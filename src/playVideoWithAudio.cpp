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

// Refresh Event
#define REFRESH_EVENT (SDL_USEREVENT + 1)

#define BREAK_EVENT (SDL_USEREVENT + 2)

#define VIDEO_FINISH (SDL_USEREVENT + 3)

namespace {

using namespace ffmpegUtil;

using std::cout;
using std::endl;

void sdlAudioCallback(void* userdata, Uint8* stream, int len) {
  AudioProcessor* receiver = (AudioProcessor*)userdata;
  receiver->writeAudioData(stream, len);
}

void pktReader(PacketGrabber& pGrabber, AudioProcessor* aProcessor,
               VideoProcessor* vProcessor) {
  const int CHECK_PERIOD = 15;

  cout << "INFO: pkt Reader thread started." << endl;
  int audioIndex = aProcessor->getAudioIndex();
  int videoIndex = vProcessor->getVideoIndex();

  while (!pGrabber.isFileEnd()) {
    while (aProcessor->needPacket() || vProcessor->needPacket()) {
      PacketDeleter d{};
      AVPacket* packet = (AVPacket*)av_malloc(sizeof(AVPacket));
      int t = pGrabber.grabPacket(packet);
      if (t == audioIndex && aProcessor != nullptr) {
        unique_ptr<AVPacket, PacketDeleter> uPacket(packet, d);
        aProcessor->pushPkt(std::move(uPacket));
      } else if (t == videoIndex && vProcessor != nullptr) {
        unique_ptr<AVPacket, PacketDeleter> uPacket(packet, d);
        vProcessor->pushPkt(std::move(uPacket));
      } else {
        av_packet_free(&packet);
        cout << "WARN: unknown streamIndex: [" << t << "]" << endl;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(CHECK_PERIOD));
  }
  cout << "INFO: pkt Reader thread finished." << endl;
}

void picRefresher(int timeInterval) {
  while (true) {
    SDL_Event event;
    event.type = REFRESH_EVENT;
    SDL_PushEvent(&event);
    std::this_thread::sleep_for(std::chrono::milliseconds(timeInterval));
  }
}

void startSdlVideo(VideoProcessor& vProcessor) {
  // ------------------- SDL video
  //--------------------- GET SDL window READY -------------------

  auto width = vProcessor.getWidth();
  auto height = vProcessor.getHeight();

  SDL_Window* screen;
  // SDL 2.0 Support for multiple windows
  screen = SDL_CreateWindow("Simplest Video Play SDL2", SDL_WINDOWPOS_UNDEFINED,
                            SDL_WINDOWPOS_UNDEFINED, width / 2, height / 2,
                            SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
  if (!screen) {
    string errMsg = "SDL: could not create window - exiting:";
    errMsg += SDL_GetError();
    cout << errMsg << endl;
    throw std::runtime_error(errMsg);
  }

  SDL_Renderer* sdlRenderer = SDL_CreateRenderer(screen, -1, 0);

  // IYUV: Y + U + V  (3 planes)
  // YV12: Y + V + U  (3 planes)
  Uint32 pixformat = SDL_PIXELFORMAT_IYUV;

  SDL_Texture* sdlTexture =
      SDL_CreateTexture(sdlRenderer, pixformat, SDL_TEXTUREACCESS_STREAMING, width, height);
  // Use this function to update a rectangle within a planar
  // YV12 or IYUV texture with new pixel data.
  SDL_Event event;

  std::thread refreshThread{picRefresher, 30};
  refreshThread.detach();

  while (true) {
    SDL_WaitEvent(&event);

    if (event.type == REFRESH_EVENT) {
      // Use this function to update a rectangle within a planar
      // YV12 or IYUV texture with new pixel data.
      auto pic = vProcessor.getFrame();
      SDL_UpdateYUVTexture(sdlTexture,    // the texture to update
        NULL,          // a pointer to the rectangle of pixels to update, or
                       // NULL to update the entire texture
        pic->data[0],  // the raw pixel data for the Y plane
        pic->linesize[0],  // the number of bytes between rows of pixel
                           // data for the Y plane
        pic->data[1],      // the raw pixel data for the U plane
        pic->linesize[1],  // the number of bytes between rows of pixel
                           // data for the U plane
        pic->data[2],      // the raw pixel data for the V plane
        pic->linesize[2]   // the number of bytes between rows of pixel
                           // data for the V plane
      );

      SDL_RenderClear(sdlRenderer);
      SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, NULL);
      SDL_RenderPresent(sdlRenderer);

      vProcessor.refreshFrame();

    } else if (event.type == SDL_QUIT) {
      //close window.
      break;
    } else if (event.type == BREAK_EVENT) {
      break;
    }


    std::this_thread::sleep_for(std::chrono::milliseconds(30));
  }
}

void startSdlAudio(AudioProcessor& aProcessor) {
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
  //wanted_specs.samples = 1152;
  wanted_specs.samples = 1024;
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
  audioProcessor.start();

  VideoProcessor videoProcessor(formatCtx);
  videoProcessor.start();

  // start pkt reader
  std::thread readerThread{pktReader, std::ref(packetGrabber), &audioProcessor,
                           &videoProcessor};

  SDL_setenv("SDL_AUDIO_ALSA_SET_BUFFER_SIZE", "1", 1);

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
    string errMsg = "Could not initialize SDL -";
    errMsg += SDL_GetError();
    cout << errMsg << endl;
    throw std::runtime_error(errMsg);
  }

  startSdlAudio(audioProcessor);

  std::thread videoThread{startSdlVideo, std::ref(videoProcessor)};

  //TODO Audio and video synchronization

  readerThread.join();
  videoThread.join();
  SDL_CloseAudio();

  return 0;
}

}  // namespace

void playVideoWithAudio(const string& inputFile) {
  std::cout << "playVideoWithAudio: " << inputFile << std::endl;

  play(inputFile);
}