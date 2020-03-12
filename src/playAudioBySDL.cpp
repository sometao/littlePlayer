#include <iostream>
#include <fstream>
#include "ffmpegUtil.h"

extern "C" {
#include "SDL2/SDL.h"
};

using std::cout;
using std::endl;
using std::string;

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

void audio_callback(void* userdata, Uint8* stream, int len) {
  void** playUtil = (void**)userdata;
  FrameGrabber* grabber = (FrameGrabber*)playUtil[0];
  ffmpegUtil::ReSampler* reSampler = (ffmpegUtil::ReSampler*)playUtil[1];

  static uint8_t* outBuffer = nullptr;
  static int outBufferSize = 0;
  static AVFrame* aFrame = av_frame_alloc();

  int ret = grabber->grabAudioFrame(aFrame);
  if (ret == 2) {
    // cout << "play with ReSampler!" << endl;
    if (outBuffer == nullptr) {
      outBufferSize = reSampler->allocDataBuf(&outBuffer, aFrame->nb_samples);
    } else {
      memset(outBuffer, 0, outBufferSize);
    }

    int outDataSize = reSampler->reSample(outBuffer, outBufferSize, aFrame);

    if (outDataSize != len) {
      cout << "WARNING: outDataSize[" << outDataSize << "] != len[" << len << "]" << endl;
    }

    std::memcpy(stream, outBuffer, outDataSize);
  }
}

void playMediaFileAudio(const string& inputPath) {
  FrameGrabber grabber{inputPath, false, true};
  grabber.start();

  int64_t inLayout = grabber.getChannleLayout();
  int inSampleRate = grabber.getSampleRate();
  int inChannels = grabber.getChannels();
  AVSampleFormat inFormat = AVSampleFormat(grabber.getSampleFormat());

  AudioInfo inAudio(inLayout, inSampleRate, inChannels, inFormat);
  AudioInfo outAudio = ReSampler::getDefaultAudioInfo();

  ReSampler reSampler(inAudio, outAudio);

  void* playUtil[2];
  playUtil[0] = &grabber;
  playUtil[1] = &reSampler;

  SDL_setenv("SDL_AUDIO_ALSA_SET_BUFFER_SIZE", "1", 1);

  if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
    string errMsg = "Could not initialize SDL -";
    errMsg += SDL_GetError();
    cout << errMsg << endl;
    throw std::runtime_error(errMsg);
  }

  // SDL_AudioInit("waveout");
  // SDL_AudioInit("dsound");

  //--------------------- GET SDL audio READY -------------------

  // audio specs containers
  SDL_AudioSpec wanted_specs;
  SDL_AudioSpec specs;

  cout << "grabber.getSampleFormat() = " << grabber.getSampleFormat() << endl;
  cout << "grabber.getSampleRate() = " << grabber.getSampleRate() << endl;
  cout << "++" << endl;

  // set audio settings from codec info
  wanted_specs.freq = grabber.getSampleRate();
  wanted_specs.format = AUDIO_S16SYS;
  wanted_specs.channels = grabber.getChannels();
  wanted_specs.samples = 1152;
  wanted_specs.callback = audio_callback;
  wanted_specs.userdata = &playUtil;

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

  SDL_Delay(300000);

  // TODO wait for the audio finish.

  SDL_CloseAudio();

  //----------------------------------
}

}  // namespace

void playAudioBySDL(const string& inputPath) {
  cout << "play video: " << inputPath << endl;

  try {
    // playYuvFile(inputPath);
    // playMediaFileVideo(inputPath);
    playMediaFileAudio(inputPath);
  } catch (std::exception ex) {
    cout << "exception: " << ex.what() << endl;
  }
}
