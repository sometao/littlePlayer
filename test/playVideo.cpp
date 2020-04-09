#include <iostream>
#include <fstream>
#include "ffmpegUtil.h"
#include "test/FrameGrabber.h"



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

}  // namespace ffmpegUtil

namespace {

using namespace ffmpegUtil;

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

void playYuvFile(const string& inputPath) {
  cout << "Hi, player sdl2." << endl;
  if (SDL_Init(SDL_INIT_VIDEO)) {
    string errMsg = "Could not initialize SDL -";
    errMsg += SDL_GetError();
    cout << errMsg << endl;
    throw std::runtime_error(errMsg);
  }

  SDL_Window* screen;
  // SDL 2.0 Support for multiple windows
  screen = SDL_CreateWindow("Simplest Video Play SDL2", SDL_WINDOWPOS_UNDEFINED,
                            SDL_WINDOWPOS_UNDEFINED, screen_w, screen_h,
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

void playMediaFileVideo(const string& inputPath) {
  FrameGrabber grabber{inputPath, true, false};
  grabber.start();

  const int w = grabber.getWidth();
  const int h = grabber.getHeight();
  const auto fmt = AVPixelFormat(grabber.getPixelFormat());

  int winWidth = w / 2;
  int winHeight = h / 2;

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
                            SDL_WINDOWPOS_UNDEFINED, winWidth, winHeight,
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
      SDL_CreateTexture(sdlRenderer, pixformat, SDL_TEXTUREACCESS_STREAMING, w, h);

  //---------------------------------------------

  std::ifstream is{inputPath, std::ios::binary};
  if (!is.is_open()) {
    string errMsg = "cannot open this file:";
    errMsg += inputPath;
    cout << errMsg << endl;
    throw std::runtime_error(errMsg);
  }

  try {
    int timeInterval = 1000 / (int)grabber.getFrameRate();

    cout << "timeInterval: " << timeInterval << endl;

    SDL_Thread* refresh_thread =
        SDL_CreateThread(refreshPicture, "refreshPictureThread", &timeInterval);

    AVFrame* frame = av_frame_alloc();
    int ret;
    bool videoFinish = false;

    SDL_Event event;

    struct SwsContext* sws_ctx =
        sws_getContext(w, h, fmt, w, h, AV_PIX_FMT_YUV420P, SWS_BILINEAR, NULL, NULL, NULL);

    int numBytes = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, w, h, 32);
    uint8_t* buffer = (uint8_t*)av_malloc(numBytes * sizeof(uint8_t));
    AVFrame* pict = av_frame_alloc();
    av_image_fill_arrays(pict->data, pict->linesize, buffer, AV_PIX_FMT_YUV420P, w, h, 32);

    while (true) {
      if (!videoFinish) {
        ret = grabber.grabImageFrame(frame);
        if (ret == 1) {  // success.
          // ffmpegUtil::writeY420pFrame2Buffer(reinterpret_cast<char*>(buffer), frame);

          sws_scale(sws_ctx, (uint8_t const* const*)frame->data, frame->linesize, 0, h,
                    pict->data, pict->linesize);

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
        // Use this function to update a rectangle within a planar
        // YV12 or IYUV texture with new pixel data.
        SDL_UpdateYUVTexture(sdlTexture,  // the texture to update
                             NULL,        // a pointer to the rectangle of pixels to update, or
                                          // NULL to update the entire texture
                             pict->data[0],      // the raw pixel data for the Y plane
                             pict->linesize[0],  // the number of bytes between rows of pixel
                                                 // data for the Y plane
                             pict->data[1],      // the raw pixel data for the U plane
                             pict->linesize[1],  // the number of bytes between rows of pixel
                                                 // data for the U plane
                             pict->data[2],      // the raw pixel data for the V plane
                             pict->linesize[2]   // the number of bytes between rows of pixel
                                                 // data for the V plane
        );

        SDL_RenderClear(sdlRenderer);
        SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, NULL);
        SDL_RenderPresent(sdlRenderer);

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

}  // namespace

void playVideo(const string& inputPath) {
  cout << "play video: " << inputPath << endl;

  try {
    // playYuvFile(inputPath);
    playMediaFileVideo(inputPath);
  } catch (std::exception ex) {
    cout << "exception: " << ex.what() << endl;
  }
}
