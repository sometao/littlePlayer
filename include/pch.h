// pch.h: This is a precompiled header file.
// Files listed below are compiled only once, improving build performance for future builds.
// This also affects IntelliSense performance, including code completion and many code browsing
// features. However, files listed here are ALL re-compiled if any one of them is updated between
// builds. Do not add files here that you will be updating frequently as this negates the
// performance advantage.

// add headers that you want to pre-compile here

#ifndef PCH_H
#define PCH_H

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

extern "C" {
#include "sdl2/SDL.h"
};

#include <iostream>
#include <string>
#include <sstream>

#endif  // PCH_H
