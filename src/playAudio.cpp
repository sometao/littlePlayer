#include <iostream>
#include "FrameGrabber.h"

#include "OpenAL/alc.h"
#include "OpenAL/al.h"
#include <chrono>
#include <thread>
#include <string>
#include <iomanip>

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

int allocOutDataBuf(uint8_t** outData, AVFrame* frame, int outSampleRate, int outChannels,
                    int bytePerOutSample) {
  int guessOutSamplesPerChannel =
      av_rescale_rnd(frame->nb_samples, outSampleRate, frame->sample_rate, AV_ROUND_UP);
  int guessOutSize = guessOutSamplesPerChannel * outChannels * bytePerOutSample;

  cout << "GuessOutSamplesPerChannel: " << guessOutSamplesPerChannel << endl;
  cout << "GuessOutSize: " << guessOutSize << endl;

  guessOutSize *= 1.2;  // Just make sure...

  *outData = (uint8_t*)av_malloc(sizeof(uint8_t) * guessOutSize);
  // av_samples_alloc(&outData, NULL, outChannels, guessOutSamplesPerChannel, AV_SAMPLE_FMT_S16, 0);
  return guessOutSize;
}

void feedAudioData(FrameGrabber* grabber, ALuint uiSource, ALuint alBufferId) {
  static struct SwrContext* swr = nullptr;
  static AVFrame* aFrame = av_frame_alloc();
  static int outBufferSize = 0;
  static uint8_t* outBuffer = nullptr;

  int64_t outLayout = AV_CH_LAYOUT_STEREO;
  int outSampleRate = 44100;
  int outChannels = 2;
  AVSampleFormat outFormat = AV_SAMPLE_FMT_S16;

  int64_t inLayout = grabber->getChannleLayout();
  int inSampleRate = grabber->getSampleRate();
  int inChannels = grabber->getChannels();
  AVSampleFormat inFormat = AVSampleFormat(grabber->getSampleFormat());

  if (swr == nullptr) {
    cout << "+++++++++++++++++++++  IN  +++++++++++++++++" << endl;
    cout << "layout: " << inLayout << endl;
    cout << "fmt: " << inFormat << endl;
    cout << "sampleRate: " << inSampleRate << endl;

    cout << "+++++++++++++++++++++  OUT  +++++++++++++++++" << endl;
    cout << "layout: " << outLayout << endl;
    cout << "fmt: " << outFormat << endl;
    cout << "sampleRate: " << outSampleRate << endl;

    swr = swr_alloc();

    swr = swr_alloc_set_opts(swr, outLayout, AV_SAMPLE_FMT_S16, outSampleRate, inLayout, inFormat,
                             inSampleRate, 0, nullptr);
    if (swr_init(swr)) {
      throw std::runtime_error("swr_init error.");
    }
  }

  unsigned long ulFormat = 0;

  if (aFrame->channels == 1) {
    ulFormat = AL_FORMAT_MONO16;
  } else {
    ulFormat = AL_FORMAT_STEREO16;
  }

  int ret = grabber->grabAudioFrame(aFrame);
  if (ret == 2) {  // got a audio frame.

    int inSamples = aFrame->nb_samples;

    if (outBuffer == nullptr) {
      outBufferSize = allocOutDataBuf(&outBuffer, aFrame, outSampleRate, outChannels, 2);
    } else {
      memset(outBuffer, 0, outBufferSize);
    }

    int outSamples =
        swr_convert(swr, &outBuffer, outBufferSize, (const uint8_t**)&aFrame->data[0], inSamples);

    if (outSamples <= 0) {
      throw std::runtime_error("error: outSamples=" + outSamples);
    }

    int outDataSize = av_samples_get_buffer_size(NULL, outChannels, outSamples, outFormat, 1);



    if (outDataSize <= 0) {
      throw std::runtime_error("error: outDataSize=" + outDataSize);
    }

    //cout << " --------------- " << endl;
    //cout << "outBufferSize:" << outBufferSize << endl;
    //cout << "outSamples:" << outSamples << endl;
    //cout << "outDataSize:" << outDataSize << endl;
    //cout << " --------------- " << endl;

    //cout << "feed data size: [" << outDataSize << "] to buffer id [" << alBufferId << "]" << endl;
    //cout << "======================================" << endl;
    //cout << "ulFormat:" << ulFormat << endl;
    //cout << "outDataSize:" << outDataSize << endl;
    //cout << "outSampleRate:" << outSampleRate << endl;
    //cout << "bufferID:" << alBufferId << endl;
    //cout << "======================================" << endl;

    alBufferData(alBufferId, ulFormat, outBuffer, outDataSize, outSampleRate);
    alSourceQueueBuffers(uiSource, 1, &alBufferId);
  }
}

int play(FrameGrabber* grabber) {
  ALuint uiSource;

  ALCdevice* pDevice;
  ALCcontext* pContext;

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

  for (int i = 0; i < NUMBUFFERS; i++) {
    cout << "buffer[" << i << "] id=" << alBufferArray[i] << endl;
  }

  cout << "--------------------" << endl;

  alGenBuffers(NUMBUFFERS, alBufferArray);

  for (int i = 0; i < NUMBUFFERS; i++) {
    cout << "buffer[" << i << "] id=" << alBufferArray[i] << endl;
  }

  // feed audio buffer first time.
  for (int i = 0; i < NUMBUFFERS; i++) {
    feedAudioData(grabber, uiSource, alBufferArray[i]);
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
    //cout << "Total Buffers Processed: " << iTotalBuffersProcessed << endl;
    //cout << "Processed: " << iBuffersProcessed << endl;

    // For each processed buffer, remove it from the Source Queue, read next
    // chunk of audio data from disk, fill buffer with new data, and add it
    // to the Source Queue
    while (iBuffersProcessed > 0) {
      bufferId = 0;
      alSourceUnqueueBuffers(uiSource, 1, &bufferId);
      feedAudioData(grabber, uiSource, bufferId);
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

int playAudio(const string& filePath) {
  FrameGrabber grabber{filePath, false, true};
  grabber.start();

  play(&grabber);

  return 0;
}
