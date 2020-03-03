#include <iostream>
#include <fstream>
#include "FrameGrabber.h"

extern "C" {
#include "SDL2/SDL.h"
};

// Refresh Event
#define REFRESH_EVENT (SDL_USEREVENT + 1)

#define BREAK_EVENT (SDL_USEREVENT + 2)

#define VIDEO_FINISH (SDL_USEREVENT + 3)

#define SDL_AUDIO_BUFFER_SIZE 1024

#define MAX_AUDIO_FRAME_SIZE 192000

using std::cout;
using std::endl;
using std::string;

namespace ffmpegUtil {
extern void writeY420pFrame2Buffer(char* buffer, AVFrame* frame);
extern int audio_resampling(AVCodecContext* aCodecCtx, AVFrame* decoded_audio_frame,
                            enum AVSampleFormat out_sample_fmt, int out_channels,
                            int out_sample_rate, uint8_t* out_buf);

}  // namespace ffmpegUtil

namespace {

const int bpp = 12;

int screen_w = 640;
int screen_h = 360;
const int pixel_w = 1920;
const int pixel_h = 1080;

const int bufferSize = pixel_w * pixel_h * bpp / 8;
unsigned char buffer[bufferSize];

int thread_exit = 0;

int refreshPicture(void* opaque) {
  int timeInterval = *((int*)opaque);
  thread_exit = 0;
  while (!thread_exit) {
    SDL_Event event;
    event.type = REFRESH_EVENT;
    SDL_PushEvent(&event);
    SDL_Delay(timeInterval);
  }
  thread_exit = 0;
  // Break
  SDL_Event event;
  event.type = BREAK_EVENT;
  SDL_PushEvent(&event);

  return 0;
}

void playYuvFile(const char* inputPath) {
  cout << "Hi, player sdl2." << endl;
  if (SDL_Init(SDL_INIT_VIDEO)) {
    string errMsg = "Could not initialize SDL -";
    errMsg += SDL_GetError();
    cout << errMsg << endl;
    throw std::runtime_error(errMsg);
  }

  SDL_Window* screen;
  // SDL 2.0 Support for multiple windows
  screen =
      SDL_CreateWindow("Simplest Video Play SDL2", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                       screen_w, screen_h, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
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
      SDL_CreateTexture(sdlRenderer, pixformat, SDL_TEXTUREACCESS_STREAMING, pixel_w, pixel_h);

  std::ifstream is{inputPath, std::ios::binary};
  if (!is.is_open()) {
    string errMsg = "cannot open this file:";
    errMsg += inputPath;
    cout << errMsg << endl;
    throw std::runtime_error(errMsg);
  }

  int timeInterval = 10;

  SDL_Thread* refresh_thread =
      SDL_CreateThread(refreshPicture, "refreshPictureThread", &timeInterval);
  SDL_Event event;
  SDL_Rect sdlRect;

  while (1) {
    // Wait
    SDL_WaitEvent(&event);
    if (event.type == REFRESH_EVENT) {
      is.read(reinterpret_cast<char*>(buffer), bufferSize);
      if (is.gcount() != bufferSize) {
        // file read finish.
        return;
      }

      SDL_UpdateTexture(sdlTexture, NULL, buffer, pixel_w);

      // FIX: If window is resize
      sdlRect.x = 0;
      sdlRect.y = 0;
      sdlRect.w = screen_w;
      sdlRect.h = screen_h;

      SDL_RenderClear(sdlRenderer);
      SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, &sdlRect);
      SDL_RenderPresent(sdlRenderer);

    } else if (event.type == SDL_WINDOWEVENT) {
      // If Resize
      SDL_GetWindowSize(screen, &screen_w, &screen_h);
    } else if (event.type == SDL_QUIT) {
      thread_exit = 1;
    } else if (event.type == BREAK_EVENT) {
      break;
    }
  }

  is.close();
  SDL_Quit();
}

void audio_callback(void* userdata, Uint8* stream, int len) {

  cout << " +++++++++++++++++++++++++++++++++++++++++ " << endl;

  AVFrame* aFrame = av_frame_alloc();

  memset(stream, 0, len);


  // TODO get audio data from grabber, and put it into stream limited by len.

  cout << "audio_callback called:" << len << endl;

  FrameGrabber* grabber = (FrameGrabber*)userdata;

  //// The size of audio_buf is 1.5 times the size of the largest audio frame
  //// that FFmpeg will give us, which gives us a nice cushion.
  static uint8_t audio_buf[1024 * 20];
  // static unsigned int audio_buf_size = 0;
  // static unsigned int audio_buf_index = 0;
  // int audio_size = -1;
  // int len1 = -1;

  while (len > 0) {
    // we have already sent all avaialble data; get more
    int ret;
    ret = grabber->grabAudioFrame(aFrame);
    if (ret == 2) {
      cout << "got a audio frame" << endl;
      cout << "audio frame nb_samples:" << aFrame->nb_samples << endl;
      cout << "audio frame sample_rate:" << aFrame->sample_rate << endl;
      cout << "audio frame channels:" << aFrame->channels << endl;
      cout << "audio frame format:" << aFrame->format << endl;

      memset(audio_buf, 0, 1024 * 20);

      int bufferSize =
          av_samples_get_buffer_size(nullptr, grabber->getChannels(), aFrame->nb_samples,
                                     AVSampleFormat::AV_SAMPLE_FMT_S16, 0);

      int out_size = ffmpegUtil::audio_resampling(
          grabber->getAudioContext(), aFrame, AVSampleFormat::AV_SAMPLE_FMT_S16,
          grabber->getChannels(), grabber->getSampleRate(), audio_buf);

      cout << "audio frame bufferSize:" << bufferSize << endl;
      cout << "audio frame out_size:" << out_size << endl;

      cout << "audio_buf[...]";
      cout << (unsigned int)audio_buf[0] << " ";
      cout << (unsigned int)audio_buf[1] << ", ";
      cout << (unsigned int)audio_buf[2] << " ";
      cout << (unsigned int)audio_buf[3] << ", ";
      cout << (unsigned int)audio_buf[4] << " ";
      cout << (unsigned int)audio_buf[5] << " ," << endl;
      
      if (out_size <= 0) {
        throw std::runtime_error("audio_resampling error. out_size=" + out_size);
      }      

      if (out_size != bufferSize) {
        throw std::runtime_error("audio_resampling error. out_size != bufferSize");
      }

      if (bufferSize > len) {
        throw std::runtime_error("too many data in buff. bufferSize=" + bufferSize);
      }


      //SDL_MixAudio(stream, (uint8_t*)audio_buf, bufferSize, SDL_MIX_MAXVOLUME);

      //SDL_MixAudioFormat(stream, (uint8_t*)audio_buf, AUDIO_S16SYS, bufferSize, 128);


      std::memcpy(stream, audio_buf, bufferSize);
      stream += bufferSize;
      len -= bufferSize;

      // std::memcpy(stream, aFrame->data[0], bufferSize);
      // stream += bufferSize;
      // len -= bufferSize;


    } else if (ret == 0) {
      cout << "FILE END, no more frame. " << endl;
    } else {
      throw std::runtime_error("grabber grabAudioFrame ERROR: ret ==" + ret);
    }
  }

  if (len != 0) {
    throw std::runtime_error("some error, len != 0");
  } else {
    cout << "audio_callback success." << endl;
  }

  cout << " -------------------------------------- " << endl;

}

void playMediaFileAudio(const string& inputPath) {
  FrameGrabber grabber{inputPath, false, true};
  grabber.start();


  SDL_setenv("SDL_AUDIO_ALSA_SET_BUFFER_SIZE", "1", 1);
  //SDL_setenv("SDL_AUDIODRIVER", "directsound", 1);
  //SDL_setenv("SDL_AUDIODRIVER", "winmm", 1);

  if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
    string errMsg = "Could not initialize SDL -";
    errMsg += SDL_GetError();
    cout << errMsg << endl;
    throw std::runtime_error(errMsg);
  }



  //SDL_AudioInit("waveout");
  //SDL_AudioInit("dsound");

  //--------------------- GET SDL audio READY -------------------

  // audio specs containers
  SDL_AudioSpec wanted_specs;
  SDL_AudioSpec specs;

  cout << "grabber.getSampleFormat() = " << grabber.getSampleFormat() << endl;
  cout << "grabber.getSampleRate() = " << grabber.getSampleRate() << endl;
  cout << "++" << endl;

  // set audio settings from codec info
  wanted_specs.freq = grabber.getSampleRate();
  // wanted_specs.format = AUDIO_S32SYS;
  // wanted_specs.format = AUDIO_F32SYS;
  wanted_specs.format = AUDIO_S16SYS;
  // wanted_specs.format = AUDIO_F32MSB;
  // wanted_specs.format = AUDIO_S16MSB;
  // wanted_specs.format = AUDIO_U16LSB;
  // wanted_specs.format = AUDIO_S16LSB;
  // wanted_specs.format = AUDIO_S16SYS;
  wanted_specs.channels = grabber.getChannels();
  wanted_specs.samples = 1152;
  wanted_specs.callback = audio_callback;
  wanted_specs.userdata = &grabber;

  // Uint32 audio device id
  SDL_AudioDeviceID audioDeviceID;

  // open audio device
  audioDeviceID = SDL_OpenAudioDevice(  // [1]
      nullptr, 0, &wanted_specs, &specs, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_CHANNELS_CHANGE);

  // SDL_OpenAudioDevice returns a valid device ID that is > 0 on success or 0 on failure
  if (audioDeviceID == 0) {
    string errMsg = "Failed to open audio device:";
    errMsg += SDL_GetError();
    cout << errMsg << endl;
    throw std::runtime_error(errMsg);
  }

  cout << "wanted_specs.freq:" << wanted_specs.freq << endl;
  //cout << "wanted_specs.format:" << wanted_specs.format << endl;
  std::printf("wanted_specs.format: Ox%X\n", wanted_specs.format);
  cout << "wanted_specs.channels:" << (int)wanted_specs.channels << endl;
  cout << "wanted_specs.samples:" << (int)wanted_specs.samples << endl;

  cout << "------------------------------------------------" << endl;

  cout << "specs.freq:" << specs.freq << endl;
  //cout << "specs.format:" << specs.format << endl;
  std::printf("specs.format: Ox%X\n", specs.format);
  cout << "specs.channels:" << (int)specs.channels << endl;
  cout << "specs.silence:" << (int)specs.silence << endl;
  cout << "specs.samples:" << (int)specs.samples << endl;

  cout << "waiting audio play..." << endl;

  SDL_PauseAudioDevice(audioDeviceID, 0);  // [2]

  SDL_Delay(10000);

  // TODO wait for the audio finish.

  SDL_CloseAudio();

  //----------------------------------
}

void playMediaFileVideo(const string& inputPath) {
  FrameGrabber grabber{inputPath, true, false};
  grabber.start();

  if (SDL_Init(SDL_INIT_VIDEO)) {
    string errMsg = "Could not initialize SDL -";
    errMsg += SDL_GetError();
    cout << errMsg << endl;
    throw std::runtime_error(errMsg);
  }

  //--------------------- GET SDL window READY -------------------

  SDL_Window* screen;
  // SDL 2.0 Support for multiple windows
  screen = SDL_CreateWindow("Simplest Video Play SDL2", SDL_WINDOWPOS_UNDEFINED,
                            SDL_WINDOWPOS_UNDEFINED, grabber.getWidth() / 2,
                            grabber.getHeight() / 2, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
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

  SDL_Texture* sdlTexture = SDL_CreateTexture(sdlRenderer, pixformat, SDL_TEXTUREACCESS_STREAMING,
                                              grabber.getWidth(), grabber.getHeight());

  //---------------------------------------------

  std::ifstream is{inputPath, std::ios::binary};
  if (!is.is_open()) {
    string errMsg = "cannot open this file:";
    errMsg += inputPath;
    cout << errMsg << endl;
    throw std::runtime_error(errMsg);
  }

  try {
    int timeInterval = (int)grabber.getFrameRate();

    cout << "timeInterval: " << timeInterval << endl;

    SDL_Thread* refresh_thread =
        SDL_CreateThread(refreshPicture, "refreshPictureThread", &timeInterval);

    AVFrame* frame = av_frame_alloc();
    int ret;
    bool videoFinish = false;

    SDL_Event event;
    SDL_Rect sdlRect;

    while (true) {
      if (!videoFinish) {
        ret = grabber.grabImageFrame(frame);
        if (ret == 1) {  // success.
          ffmpegUtil::writeY420pFrame2Buffer(reinterpret_cast<char*>(buffer), frame);
        } else if (ret == 0) {  // no more frame.
          cout << "VIDEO FINISHED." << endl;
          videoFinish = true;
          SDL_Event finishEvent;
          finishEvent.type = VIDEO_FINISH;
          SDL_PushEvent(&finishEvent);
        } else {  // error.
          string errMsg = "grabImageFrame error.";
          cout << errMsg << endl;
          throw std::runtime_error(errMsg);
        }
      } else {
        thread_exit = 1;
        break;
      }

      // WAIT USER EVENT.
      SDL_WaitEvent(&event);
      if (event.type == REFRESH_EVENT) {
        SDL_UpdateTexture(sdlTexture, NULL, buffer, pixel_w);

        // FIX: If window is resize
        sdlRect.x = 0;
        sdlRect.y = 0;
        sdlRect.w = screen_w;
        sdlRect.h = screen_h;

        SDL_RenderClear(sdlRenderer);
        SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, &sdlRect);
        SDL_RenderPresent(sdlRenderer);

      } else if (event.type == SDL_WINDOWEVENT) {
        // If Resize
        SDL_GetWindowSize(screen, &screen_w, &screen_h);
      } else if (event.type == SDL_QUIT) {
        thread_exit = 1;
      } else if (event.type == BREAK_EVENT) {
        break;
      }
    }
    av_frame_free(&frame);
  } catch (std::exception ex) {
    cout << "Exception in play media file:" << ex.what() << endl;
  } catch (...) {
    cout << "Unknown exception in play media" << endl;
  }

  grabber.close();
}

void playMp3File(const string& inputPath) {}

}  // namespace

void playVideo(const char* inputPath) {
  cout << "play video: " << inputPath << endl;

  try {
    // playYuvFile(inputPath);
    // playMediaFileVideo(inputPath);
    playMediaFileAudio(inputPath);
  } catch (std::exception ex) {
    cout << "exception: " << ex.what() << endl;
  }
}
