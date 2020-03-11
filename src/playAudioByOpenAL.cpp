#include <iostream>
#include "FrameGrabber.h"

#include "OpenAL/alc.h"
#include "OpenAL/al.h"
#include <chrono>
#include <thread>
#include <string>
#include <iomanip>

#include "ffmpegUtil.hpp"

extern "C" {
#include <libswresample/swresample.h>
}

#define NUMBUFFERS (12)
#define SERVICE_UPDATE_PERIOD (20)

namespace {

using std::cout;
using std::endl;
using std::string;

int initSource(ALuint* pSource) {
  alGenSources(1, pSource);
  ALuint uiSource = *pSource;
  if (alGetError() != AL_NO_ERROR) {
    printf("Error generating audio source.");
    return -1;
  }
  ALfloat SourcePos[] = {0.0, 0.0, 0.0};
  ALfloat SourceVel[] = {0.0, 0.0, 0.0};
  ALfloat ListenerPos[] = {0.0, 0, 0};
  ALfloat ListenerVel[] = {0.0, 0.0, 0.0};
  // first 3 elements are "at", second 3 are "up"
  ALfloat ListenerOri[] = {0.0, 0.0, -1.0, 0.0, 1.0, 0.0};
  alSourcef(uiSource, AL_PITCH, 1.0);
  alSourcef(uiSource, AL_GAIN, 1.0);
  alSourcefv(uiSource, AL_POSITION, SourcePos);
  alSourcefv(uiSource, AL_VELOCITY, SourceVel);
  alSourcef(uiSource, AL_REFERENCE_DISTANCE, 50.0f);
  alSourcei(uiSource, AL_LOOPING, AL_FALSE);
}


void feedAudioData(FrameGrabber* grabber, ffmpegUtils::ReSampler* reSampler, ALuint uiSource,
                   ALuint alBufferId) {
  static uint8_t* outBuffer = nullptr;
  static int outBufferSize = 0;
  static AVFrame* aFrame = av_frame_alloc();

  int ret = grabber->grabAudioFrame(aFrame);
  if (ret == 2) {
    //cout << "play with ReSampler!" << endl;
    if (outBuffer == nullptr) {
      outBufferSize = reSampler->allocDataBuf(&outBuffer, aFrame->nb_samples);
    } else {
      memset(outBuffer, 0, outBufferSize);
    }

    int outDataSize = reSampler->reSample(outBuffer, outBufferSize, aFrame);

    unsigned long ulFormat = 0;

    if (aFrame->channels == 1) {
      ulFormat = AL_FORMAT_MONO16;
    } else {
      ulFormat = AL_FORMAT_STEREO16;
    }
    alBufferData(alBufferId, ulFormat, outBuffer, outDataSize, reSampler->out.sampleRate);
    alSourceQueueBuffers(uiSource, 1, &alBufferId);
  }

}

int play(FrameGrabber* grabber) {
  ALuint uiSource;

  ALCdevice* pDevice;
  ALCcontext* pContext;

  int64_t inLayout = grabber->getChannleLayout();
  int inSampleRate = grabber->getSampleRate();
  int inChannels = grabber->getChannels();
  AVSampleFormat inFormat = AVSampleFormat(grabber->getSampleFormat());

  ffmpegUtils::AudioInfo inAudio(inLayout, inSampleRate, inChannels, inFormat);
  ffmpegUtils::AudioInfo outAudio = ffmpegUtils::ReSampler::getDefaultAudioInfo();

  ffmpegUtils::ReSampler reSampler(inAudio, outAudio);

  pDevice = alcOpenDevice(NULL);
  pContext = alcCreateContext(pDevice, NULL);
  alcMakeContextCurrent(pContext);

  if (alcGetError(pDevice) != ALC_NO_ERROR) return AL_FALSE;

  alGenSources(1, &uiSource);

  if (alGetError() != AL_NO_ERROR) {
    printf("Error generating audio source.");
    return -1;
  }
  ALfloat SourcePos[] = {0.0, 0.0, 0.0};
  ALfloat SourceVel[] = {0.0, 0.0, 0.0};
  ALfloat ListenerPos[] = {0.0, 0, 0};
  ALfloat ListenerVel[] = {0.0, 0.0, 0.0};
  // first 3 elements are "at", second 3 are "up"
  ALfloat ListenerOri[] = {0.0, 0.0, -1.0, 0.0, 1.0, 0.0};
  alSourcef(uiSource, AL_PITCH, 1.0);
  alSourcef(uiSource, AL_GAIN, 1.0);
  alSourcefv(uiSource, AL_POSITION, SourcePos);
  alSourcefv(uiSource, AL_VELOCITY, SourceVel);
  alSourcef(uiSource, AL_REFERENCE_DISTANCE, 50.0f);
  alSourcei(uiSource, AL_LOOPING, AL_FALSE);

  alDistanceModel(AL_LINEAR_DISTANCE_CLAMPED);
  alListener3f(AL_POSITION, 0, 0, 0);

  ALuint alBufferArray[NUMBUFFERS];

  alGenBuffers(NUMBUFFERS, alBufferArray);

  // feed audio buffer first time.
  for (int i = 0; i < NUMBUFFERS; i++) {
    feedAudioData(grabber, &reSampler, uiSource, alBufferArray[i]);
  }

  // Start playing source
  alSourcePlay(uiSource);

  ALint iTotalBuffersProcessed = 0;
  ALint iBuffersProcessed;
  ALint iState;
  ALuint bufferId;
  ALint iQueuedBuffers;
  while (true) {
    std::this_thread::sleep_for(std::chrono::milliseconds(SERVICE_UPDATE_PERIOD));

    // Request the number of OpenAL Buffers have been processed (played) on
    // the Source
    iBuffersProcessed = 0;
    alGetSourcei(uiSource, AL_BUFFERS_PROCESSED, &iBuffersProcessed);
    iTotalBuffersProcessed += iBuffersProcessed;

    // Keep a running count of number of buffers processed (for logging
    // purposes only)
    iTotalBuffersProcessed += iBuffersProcessed;
    // cout << "Total Buffers Processed: " << iTotalBuffersProcessed << endl;
    // cout << "Processed: " << iBuffersProcessed << endl;

    // For each processed buffer, remove it from the Source Queue, read next
    // chunk of audio data from disk, fill buffer with new data, and add it
    // to the Source Queue
    while (iBuffersProcessed > 0) {
      bufferId = 0;
      alSourceUnqueueBuffers(uiSource, 1, &bufferId);
      feedAudioData(grabber, &reSampler, uiSource, bufferId);
      iBuffersProcessed -= 1;
    }

    // Check the status of the Source.  If it is not playing, then playback
    // was completed, or the Source was starved of audio data, and needs to
    // be restarted.
    alGetSourcei(uiSource, AL_SOURCE_STATE, &iState);

    if (iState != AL_PLAYING) {
      // If there are Buffers in the Source Queue then the Source was
      // starved of audio data, so needs to be restarted (because there is
      // more audio data to play)
      alGetSourcei(uiSource, AL_BUFFERS_QUEUED, &iQueuedBuffers);
      if (iQueuedBuffers) {
        alSourcePlay(uiSource);
      } else {
        // Finished playing
        break;
      }
    }
  }

  // Stop the Source and clear the Queue
  alSourceStop(uiSource);
  alSourcei(uiSource, AL_BUFFER, 0);

  // Clean up buffers and sources
  alDeleteSources(1, &uiSource);
  alDeleteBuffers(NUMBUFFERS, alBufferArray);

  return 0;
}

}  // namespace

void playAudioByOpenAL(const string& filePath) {
  FrameGrabber grabber{filePath, false, true};
  grabber.start();

  play(&grabber);

}
