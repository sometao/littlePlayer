/**
@author Tao Zhang
@since 2020/3/1
@version 0.0.1-SNAPSHOT 2020/5/13
*/
#include "pch.h"
#include "ffmpegUtil.h"

#include <iostream>
#include <string>
#include <list>
#include <memory>
#include <chrono>
#include <thread>
#include "MediaProcessor.hpp"

extern "C" {
#include "SDL/SDL.h"
};

// Refresh Event
#define REFRESH_EVENT (SDL_USEREVENT + 1)

#define BREAK_EVENT (SDL_USEREVENT + 3)

#define VIDEO_FINISH (SDL_USEREVENT + 4)

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
  const int CHECK_PERIOD = 10;

  cout << "INFO: pkt Reader thread started." << endl;
  int audioIndex = aProcessor->getAudioIndex();
  int videoIndex = vProcessor->getVideoIndex();

  while (!pGrabber.isFileEnd() && !aProcessor->isClosed() && !vProcessor->isClosed()) {
    while (aProcessor->needPacket() || vProcessor->needPacket()) {
      AVPacket* packet = (AVPacket*)av_malloc(sizeof(AVPacket));
      int t = pGrabber.grabPacket(packet);
      if (t == -1) {
        cout << "INFO: file finish." << endl;
        aProcessor->pushPkt(nullptr);
        vProcessor->pushPkt(nullptr);
        break;
      } else if (t == audioIndex && aProcessor != nullptr) {
        unique_ptr<AVPacket> uPacket(packet);
        aProcessor->pushPkt(std::move(uPacket));
      } else if (t == videoIndex && vProcessor != nullptr) {
        unique_ptr<AVPacket> uPacket(packet);
        vProcessor->pushPkt(std::move(uPacket));
      } else {
        av_packet_free(&packet);
        cout << "WARN: unknown streamIndex: [" << t << "]" << endl;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(CHECK_PERIOD));
  }
  cout << "[THREAD] INFO: pkt Reader thread finished." << endl;
}

void picRefresher(int timeInterval, bool& exitRefresh, bool& faster) {
  cout << "picRefresher timeInterval[" << timeInterval << "]" << endl;
  while (!exitRefresh) {
    SDL_Event event;
    event.type = REFRESH_EVENT;
    SDL_PushEvent(&event);
    if (faster) {
      std::this_thread::sleep_for(std::chrono::milliseconds(timeInterval / 2));
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(timeInterval));
    }
  }
  cout << "[THREAD] picRefresher thread finished." << endl;
}

void playSdlVideo(VideoProcessor& vProcessor, AudioProcessor* audio = nullptr) {
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
  auto frameRate = vProcessor.getFrameRate();
  cout << "frame rate [" << frameRate << "]" << endl;

  bool exitRefresh = false;
  bool faster = false;
  std::thread refreshThread{picRefresher, (int)(1000 / frameRate), std::ref(exitRefresh),
                            std::ref(faster)};

  int failCount = 0;
  int fastCount = 0;
  int slowCount = 0;
  while (!vProcessor.isStreamFinished()) {
    SDL_WaitEvent(&event);

    if (event.type == REFRESH_EVENT) {
      if (vProcessor.isStreamFinished()) {
        exitRefresh = true;
        continue;  // skip REFRESH event.
      }

      if (audio != nullptr) {
        auto vTs = vProcessor.getPts();
        auto aTs = audio->getPts();
        if (vTs > aTs && vTs - aTs > 30) {
          cout << "VIDEO FASTER ================= vTs - aTs [" << (vTs - aTs)
               << "]ms, SKIP A EVENT" << endl;
          // skip a REFRESH_EVENT
          faster = false;
          slowCount++;
          continue;
        } else if (vTs < aTs && aTs - vTs > 30) {
          cout << "VIDEO SLOWER ================= aTs - vTs =[" << (aTs - vTs) << "]ms, Faster"
               << endl;
          faster = true;
          fastCount++;
        } else {
          faster = false;
          // cout << "=================   vTs[" << vTs << "] aPts[" << aTs << "] nothing to
          // do." << endl;
        }
      }

      // Use this function to update a rectangle within a planar
      // YV12 or IYUV texture with new pixel data.
      AVFrame* frame = vProcessor.getFrame();

      if (frame != nullptr) {
        SDL_UpdateYUVTexture(sdlTexture,  // the texture to update
                             NULL,        // a pointer to the rectangle of pixels to update, or
                                          // NULL to update the entire texture
                             frame->data[0],      // the raw pixel data for the Y plane
                             frame->linesize[0],  // the number of bytes between rows of pixel
                                                // data for the Y plane
                             frame->data[1],      // the raw pixel data for the U plane
                             frame->linesize[1],  // the number of bytes between rows of pixel
                                                // data for the U plane
                             frame->data[2],      // the raw pixel data for the V plane
                             frame->linesize[2]   // the number of bytes between rows of pixel
                                                // data for the V plane
        );
        SDL_RenderClear(sdlRenderer);
        SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, NULL);
        SDL_RenderPresent(sdlRenderer);

        if (!vProcessor.refreshFrame()) {
          cout << "WARN: vProcessor.refreshFrame false" << endl;
        }
      } else {
        failCount++;
        cout << "WARN: getFrame fail. failCount = " << failCount << endl;
      }

    } else if (event.type == SDL_QUIT) {
      cout << "SDL screen got a SDL_QUIT." << endl;
      exitRefresh = true;
      // close window.
      break;
    } else if (event.type == BREAK_EVENT) {
      break;
    }
  }

  refreshThread.join();
  cout << "[THREAD] Sdl video thread finish: failCount = " << failCount << ", fastCount = " << fastCount
       << ", slowCount = " << slowCount << endl;
}

void startSdlAudio(SDL_AudioDeviceID& audioDeviceID, AudioProcessor& aProcessor) {
  //--------------------- GET SDL audio READY -------------------

  // audio specs containers
  SDL_AudioSpec wanted_specs;
  SDL_AudioSpec specs;

  cout << "aProcessor.getSampleFormat() = " << aProcessor.getSampleFormat() << endl;
  cout << "aProcessor.getSampleRate() = " << aProcessor.getOutSampleRate() << endl;
  cout << "aProcessor.getChannels() = " << aProcessor.getOutChannels() << endl;
  cout << "++" << endl;

  int samples = -1;
  while (true) {
    cout << "getting audio samples." << endl;
    samples = aProcessor.getSamples();
    if (samples <= 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } else {
      cout << "get audio samples:" << samples << endl;
      break;
    }
  }

  // set audio settings from codec info
  wanted_specs.freq = aProcessor.getOutSampleRate();
  wanted_specs.format = AUDIO_S16SYS;
  wanted_specs.channels = aProcessor.getOutChannels();
  wanted_specs.samples = samples;
  wanted_specs.callback = sdlAudioCallback;
  wanted_specs.userdata = &aProcessor;

  // open audio device
  audioDeviceID = SDL_OpenAudioDevice(nullptr, 0, &wanted_specs, &specs, 0);

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


  SDL_PauseAudioDevice(audioDeviceID, 0);
  cout << "[THREAD] audio start thread finish." << endl;
}

int play_debug(const string& inputFile) {
  // create packet grabber
  PacketGrabber packetGrabber{ inputFile };
  auto formatCtx = packetGrabber.getFormatCtx();
  av_dump_format(formatCtx, 0, "", 0);

  VideoProcessor videoProcessor(formatCtx);
  videoProcessor.start();
  cout << " ---   1   ---------- " << endl;

  AudioProcessor audioProcessor(formatCtx);
  audioProcessor.start();
  cout << " ---   2   ---------- " << endl;

  std::thread readerThread{ pktReader, std::ref(packetGrabber), &audioProcessor, &videoProcessor };

  cout << " ---   3   ---------- " << endl;
  videoProcessor.close();
  audioProcessor.close();
  cout << " ---   4   ---------- " << endl;
  cout << " ---   5   ---------- " << endl;
  readerThread.join();
  cout << " ---   6   ---------- " << endl;

  return 0;

}

int play(const string& inputFile) {
  // create packet grabber
  PacketGrabber packetGrabber{inputFile};
  auto formatCtx = packetGrabber.getFormatCtx();
  av_dump_format(formatCtx, 0, "", 0);

  VideoProcessor videoProcessor(formatCtx);
  videoProcessor.start();

  // create AudioProcessor
  AudioProcessor audioProcessor(formatCtx);
  audioProcessor.start();



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
  

  SDL_AudioDeviceID audioDeviceID;

  std::thread startAudioThread(startSdlAudio, std::ref(audioDeviceID),
                               std::ref(audioProcessor));
  startAudioThread.join();

  std::thread videoThread{playSdlVideo, std::ref(videoProcessor), &audioProcessor};


  cout << "videoThread join." << endl;
  videoThread.join();

  SDL_PauseAudioDevice(audioDeviceID, 1);
  SDL_CloseAudio();

  bool r;
  r = audioProcessor.close();
  cout << "audioProcessor closed: " << r << endl;
  r = videoProcessor.close();
  cout << "videoProcessor closed: " << r << endl;

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  readerThread.join();
  cout << "Pause and Close audio" << endl;

  return 0;

}

}  // namespace

void playVideoWithAudio(const string& inputFile) {
  std::cout << "playVideoWithAudio: " << inputFile << std::endl;

  play(inputFile);
}