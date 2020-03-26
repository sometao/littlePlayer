#include "ffmpegUtil.h"

#include <iostream>
#include <string>
#include <list>
#include <memory>
#include <chrono>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <mutex>

using std::condition_variable;
using std::cout;
using std::endl;
using std::list;
using std::mutex;
using std::shared_ptr;
using std::string;
using std::unique_ptr;

// FIXME no need PacketDeleter
struct PacketDeleter {  // É¾³ýÆ÷
  void operator()(AVPacket* p) const { av_packet_free(&p); };
};

// FIXME no need PacketReceiver?
class PacketReceiver {
 public:
  virtual void pushPkt(unique_ptr<AVPacket, PacketDeleter> pkt) = 0;
};

class MediaProcessor : public PacketReceiver {
  list<unique_ptr<AVPacket, PacketDeleter>> packetList{};
  mutex pktListMutex{};
  int PKT_WAITING_SIZE = 3;
  bool started = false;
  bool streamFinished = false;

  AVFrame* nextFrame = av_frame_alloc();
  AVPacket* targetPkt = nullptr;

  void nextFrameKeeper() {
    int updatePeriod = 10;
    // FIXME use lock with cv instead of isNextDataReady check every 'updatePeriod'.
    while (!streamFinished) {
      if (!isNextDataReady.load()) {
        prepareNextData();
      } else {
        std::this_thread::sleep_for(std::chrono::milliseconds(updatePeriod));
      }
    }
    cout << "next frame keeper finished, index=" << streamIndex << endl;
  }

 protected:
  std::atomic<uint64_t> currentTimestamp{0};
  std::atomic<uint64_t> nextFrameTimestamp{0};
  AVRational streamTimeBase{1, 0};
  bool noMorePkt = false;

  int streamIndex = -1;
  AVCodecContext* codecCtx = nullptr;

  condition_variable cv{};
  mutex nextDataMutex{};

  std::atomic<bool> isNextDataReady{false};

  virtual void generateNextData(AVFrame* f) = 0;

  unique_ptr<AVPacket, PacketDeleter> getNextPkt() {
    if (noMorePkt) {
      return nullptr;
    }
    pktListMutex.lock();
    if (packetList.empty()) {
      pktListMutex.unlock();
      return nullptr;
    } else {
      auto pkt = std::move(packetList.front());
      if (pkt == nullptr) {
        noMorePkt = true;
        pktListMutex.unlock();
        return nullptr;
      } else {
        packetList.pop_front();
        pktListMutex.unlock();
        return pkt;
      }
    }
  }

  void prepareNextData() {
    // FIXME deal with no next data.
    while (!isNextDataReady.load() && !streamFinished) {
      if (targetPkt == nullptr) {

        if (!noMorePkt) {
          auto pkt = getNextPkt();
          if (pkt != nullptr) {
            targetPkt = pkt.release();
          } else if (noMorePkt) {
            targetPkt = nullptr;
          } else {
            return;
          }
        } else {
          // no more pkt.
          cout << "++++++++ no more pkt index=" << streamIndex
               << " finished=" << streamFinished << endl;
        }
      }

      int ret = -1;
      ret = avcodec_send_packet(codecCtx, targetPkt);
      if (ret == 0) {
        av_packet_free(&targetPkt);
        targetPkt = nullptr;
        // cout << "[AUDIO] avcodec_send_packet success." << endl;
      } else if (ret == AVERROR(EAGAIN)) {
        // buff full, can not decode any more, nothing need to do.
        // keep the packet for next time decode.
      } else if (ret == AVERROR_EOF) {
        // no new packets can be sent to it, it is safe.
        cout << "[WARN]  no new packets can be sent to it. index=" << streamIndex << endl;
      } else {
        string errorMsg = "+++++++++ ERROR avcodec_send_packet error: ";
        errorMsg += ret;
        cout << errorMsg << endl;
        throw std::runtime_error(errorMsg);
      }

      ret = avcodec_receive_frame(codecCtx, nextFrame);
      if (ret == 0) {
        // cout << "avcodec_receive_frame success." << endl;
        // success.
        generateNextData(nextFrame);
        isNextDataReady.store(true);
      } else if (ret == AVERROR_EOF) {
        cout << "+++++++++++++++++++++++++++++ MediaProcessor no more output frames. index="
             << streamIndex << endl;
        streamFinished = true;
      } else if (ret == AVERROR(EAGAIN)) {
        // need more packet.
      } else {
        string errorMsg = "avcodec_receive_frame error: ";
        errorMsg += ret;
        cout << errorMsg << endl;
        throw std::runtime_error(errorMsg);
      }
    }
  }

 public:
  void start() {
    started = true;
    std::thread keeper{&MediaProcessor::nextFrameKeeper, this};
    keeper.detach();
  }

  void pushPkt(unique_ptr<AVPacket, PacketDeleter> pkt) override {
    pktListMutex.lock();
    packetList.push_back(std::move(pkt));
    pktListMutex.unlock();
  }
  bool isStreamFinished() { return streamFinished; }

  bool needPacket() { return packetList.size() < PKT_WAITING_SIZE; }

  uint64_t getPts() { return currentTimestamp.load(); }
};

class AudioProcessor : public MediaProcessor {
  ffmpegUtil::ReSampler* reSampler = nullptr;

  uint8_t* outBuffer = nullptr;
  int outBufferSize = -1;
  int outDataSize = -1;
  int outSamples = -1;

 protected:
  void generateNextData(AVFrame* frame) final override {
    if (outBuffer == nullptr) {
      outBufferSize = reSampler->allocDataBuf(&outBuffer, frame->nb_samples);
    } else {
      memset(outBuffer, 0, outBufferSize);
    }
    std::tie(outSamples, outDataSize) = reSampler->reSample(outBuffer, outBufferSize, frame);
    auto t = frame->pts * av_q2d(streamTimeBase) * 1000;
    nextFrameTimestamp.store((uint64_t)t);
  }

 public:
  AudioProcessor(AVFormatContext* formatCtx) {
    for (int i = 0; i < formatCtx->nb_streams; i++) {
      if (formatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
        streamTimeBase = formatCtx->streams[i]->time_base;
        streamIndex = i;
        cout << "audio stream index = : [" << i << "] tb.n" << streamTimeBase.num << endl;
        break;
      }
    }
    if (streamIndex < 0) {
      cout << "WARN: can not find audio stream." << endl;
    }

    ffmpegUtil::ffUtils::initCodecContext(formatCtx, streamIndex, &codecCtx);

    int64_t inLayout = codecCtx->channel_layout;
    int inSampleRate = codecCtx->sample_rate;
    int inChannels = codecCtx->channels;
    AVSampleFormat inFormat = codecCtx->sample_fmt;

    ffmpegUtil::AudioInfo inAudio(inLayout, inSampleRate, inChannels, inFormat);
    ffmpegUtil::AudioInfo outAudio = ffmpegUtil::ReSampler::getDefaultAudioInfo(inSampleRate);

    reSampler = new ffmpegUtil::ReSampler(inAudio, outAudio);
  }

  int getAudioIndex() const { return streamIndex; }

  int getSamples() { return outSamples; }

  void writeAudioData(uint8_t* stream, int len) {
    static uint8_t* silenceBuff = nullptr;
    if (silenceBuff == nullptr) {
      silenceBuff = (uint8_t*)av_malloc(sizeof(uint8_t) * len);
      std::memset(silenceBuff, 0, len);
    }

    if (isNextDataReady.load()) {
      currentTimestamp.store(nextFrameTimestamp.load());
      if (outDataSize != len) {
        cout << "WARNING: outDataSize[" << outDataSize << "] != len[" << len << "]" << endl;
      }
      std::memcpy(stream, outBuffer, outDataSize);
      isNextDataReady.store(false);
      // TODO notify prepare data.
    } else {
      // if list is empty, silent will be written.
      cout << "WARNING: writeAudioData, audio data not ready." << endl;
      std::memcpy(stream, silenceBuff, len);
      return;
    }
  }

  int getChannels() const {
    if (codecCtx != nullptr) {
      return codecCtx->channels;
    } else {
      throw std::runtime_error("can not getChannels.");
    }
  }

  int getChannleLayout() const {
    if (codecCtx != nullptr) {
      return codecCtx->channel_layout;
    } else {
      throw std::runtime_error("can not getChannleLayout.");
    }
  }

  int getSampleRate() const {
    if (codecCtx != nullptr) {
      return codecCtx->sample_rate;
    } else {
      throw std::runtime_error("can not getSampleRate.");
    }
  }

  int getSampleFormat() const {
    if (codecCtx != nullptr) {
      return (int)codecCtx->sample_fmt;
    } else {
      throw std::runtime_error("can not getSampleRate.");
    }
  }
};

class VideoProcessor : public MediaProcessor {
  struct SwsContext* sws_ctx = nullptr;
  AVFrame* outPic = nullptr;

 protected:
  void generateNextData(AVFrame* frame) override {
    // TODO lock/consume/unlock/notify
    // lock nextFrame
    auto t = frame->pts * av_q2d(streamTimeBase) * 1000;
    nextFrameTimestamp.store((uint64_t)t);
    sws_scale(sws_ctx, (uint8_t const* const*)frame->data, frame->linesize, 0,
              codecCtx->height, outPic->data, outPic->linesize);
    // unlock nextFrame
  }

 public:
  VideoProcessor(AVFormatContext* formatCtx) {
    for (int i = 0; i < formatCtx->nb_streams; i++) {
      if (formatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
        streamIndex = i;
        streamTimeBase = formatCtx->streams[i]->time_base;
        cout << "video stream index = : [" << i << "] tb.n" << streamTimeBase.num << endl;
        break;
      }
    }

    if (streamIndex < 0) {
      cout << "WARN: can not find video stream." << endl;
    }

    ffmpegUtil::ffUtils::initCodecContext(formatCtx, streamIndex, &codecCtx);

    int w = codecCtx->width;
    int h = codecCtx->height;

    sws_ctx = sws_getContext(w, h, codecCtx->pix_fmt, w, h, AV_PIX_FMT_YUV420P, SWS_BILINEAR,
                             NULL, NULL, NULL);

    int numBytes = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, w, h, 32);
    outPic = av_frame_alloc();
    uint8_t* buffer = (uint8_t*)av_malloc(numBytes * sizeof(uint8_t));
    av_image_fill_arrays(outPic->data, outPic->linesize, buffer, AV_PIX_FMT_YUV420P, w, h, 32);
  }

  int getVideoIndex() const { return streamIndex; }

  AVFrame* getFrame() {
    if (isNextDataReady.load()) {
      currentTimestamp.store(nextFrameTimestamp.load());
      return outPic;
    } else {
      cout << "WARNING: getFrame, video data not ready." << endl;
      return nullptr;
    }
  }

  bool refreshFrame() {
    if (isNextDataReady.load()) {
      currentTimestamp.store(nextFrameTimestamp.load());
      isNextDataReady.store(false);
      // TODO notify prepare data.
      return true;
    } else {
      return false;
    }
  }

  int getWidth() const {
    if (codecCtx != nullptr) {
      return codecCtx->width;
    } else {
      throw std::runtime_error("can not getWidth.");
    }
  }

  int getHeight() const {
    if (codecCtx != nullptr) {
      return codecCtx->height;
    } else {
      throw std::runtime_error("can not getHeight.");
    }
  }

  double getFrameRate() const {
    if (codecCtx != nullptr) {
      auto frameRate = codecCtx->framerate;
      double fr = frameRate.num && frameRate.den ? av_q2d(frameRate) : 0.0;
      return fr;
    } else {
      throw std::runtime_error("can not getFrameRate.");
    }
  }
};
