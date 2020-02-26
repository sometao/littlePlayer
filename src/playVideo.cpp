#include <iostream>
#include <fstream>
#include "FrameGrabber.h"

extern "C" {
#include "sdl2/SDL.h"
};

// Refresh Event
#define REFRESH_EVENT (SDL_USEREVENT + 1)

#define BREAK_EVENT (SDL_USEREVENT + 2)

using std::cout;
using std::endl;
using std::string;

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
  int timeInterval = *((int*) opaque);
  cout << "!!  timeInterval: " << timeInterval << endl; 
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


void play(const char* inputPath) {
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

  SDL_Thread* refresh_thread = SDL_CreateThread(refreshPicture, "refreshPictureThread", &timeInterval);
  SDL_Event event;
  SDL_Rect sdlRect;

  while (1) {
    // Wait
    SDL_WaitEvent(&event);
    if (event.type == REFRESH_EVENT) {
      is.read(reinterpret_cast<char*>(buffer), bufferSize);
      if (is.gcount() != bufferSize) {
        //file read finish.
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

}  // namespace

void playVideo(const char* inputPath) {
  cout << "play video: " << inputPath << endl;
  play(inputPath);
}
