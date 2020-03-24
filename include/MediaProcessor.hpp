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
  void operator()(AVPacket* p) const { av_free_packet(p); };
};

// FIXME no need PacketReceiver?
class PacketReceiver {
 public:
  virtual void pushPkt(unique_ptr<AVPacket, PacketDeleter> pkt) = 0;
};

class MediaProcessor : public PacketReceiver {
  mutex pktListMutex{};
  int PKT_WAITING_SIZE = 3;
  bool started = false;
  list<unique_ptr<AVPacket, PacketDeleter>> packetList{};

  void nextFrameKeeper() {
    int updatePeriod = 10;
    // FIXME use lock with cv instead of isNextDataReady check every 'updatePeriod'.
    while (true) {
      if (!isNextFrameReady.load()) {
        prepareNextFrame();
      } else {
        std::this_thread::sleep_for(std::chrono::milliseconds(updatePeriod));
      }
    }
  }

 protected:
  bool streamFinished = false;

  condition_variable cv{};
  mutex nextDataMutex{};

  std::atomic<bool> isNextFrameReady{false};

  unique_ptr<AVPacket, PacketDeleter> getNextPkt() {
    pktListMutex.lock();
    if (packetList.empty()) {
      pktListMutex.unlock();
      return nullptr;
    } else {
      auto pkt = std::move(packetList.front());
      packetList.pop_front();
      pktListMutex.unlock();
      return pkt;
    }
  }

  // implement prepareNextFrame in MediaProcessor instead subClass.
  virtual void prepareNextFrame() = 0;

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

  bool needPacket() { return packetList.size() < PKT_WAITING_SIZE; }
};

class AudioProcessor : public MediaProcessor {
  int audioIndex = -1;
  AVCodecContext* aCodecCtx = nullptr;
  ffmpegUtil::ReSampler* reSampler = nullptr;
  uint8_t* outBuffer = nullptr;
  int outBufferSize = -1;

  AVFrame* nextFrame = av_frame_alloc();

  AVPacket* targetPkt = nullptr;

 protected:
  void prepareNextFrame() override {
    // FIXME deal with no next data.
    while (!isNextFrameReady.load() && !streamFinished) {
      if (targetPkt == nullptr) {
        auto pkt = getNextPkt();
        if (pkt == nullptr) {
          cout << "WARN: no next pkt." << endl;
          return;
        }
        targetPkt = pkt.release();
      }

      int ret = -1;
      ret = avcodec_send_packet(aCodecCtx, targetPkt);
      if (ret == 0) {
        av_packet_free(&targetPkt);
        targetPkt = nullptr;
        // cout << "[AUDIO] avcodec_send_packet success." << endl;
      } else if (ret == AVERROR(EAGAIN)) {
        // buff full, can not decode any more, nothing need to do.
        // keep the packet for next time decode.
      } else {
        string errorMsg = "[AUDIO] avcodec_send_packet error: ";
        errorMsg += ret;
        cout << errorMsg << endl;
        throw std::runtime_error(errorMsg);
      }

      ret = avcodec_receive_frame(aCodecCtx, nextFrame);
      if (ret == 0) {
        // cout << "[AUDIO] avcodec_receive_frame success." << endl;
        // success.
        isNextFrameReady.store(true);
      } else if (ret == AVERROR_EOF) {
        cout << "no more output frames." << endl;
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
  AudioProcessor(AVFormatContext* formatCtx) {
    for (int i = 0; i < formatCtx->nb_streams; i++) {
      if (formatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
        audioIndex = i;
        cout << "audio stream index = : [" << i << "]" << endl;
        break;
      }
    }

    ffmpegUtil::ffUtils::initCodecContext(formatCtx, audioIndex, &aCodecCtx);

    int64_t inLayout = aCodecCtx->channel_layout;
    int inSampleRate = aCodecCtx->sample_rate;
    int inChannels = aCodecCtx->channels;
    AVSampleFormat inFormat = aCodecCtx->sample_fmt;

    ffmpegUtil::AudioInfo inAudio(inLayout, inSampleRate, inChannels, inFormat);
    ffmpegUtil::AudioInfo outAudio = ffmpegUtil::ReSampler::getDefaultAudioInfo();

    reSampler = new ffmpegUtil::ReSampler(inAudio, outAudio);
  }

  int getAudioIndex() const { return audioIndex; }

  void writeAudioData(uint8_t* stream, int len) {
    if (outBuffer == nullptr) {
      if (isNextFrameReady.load()) {
        outBufferSize = reSampler->allocDataBuf(&outBuffer, nextFrame->nb_samples);
      } else {
        cout << "no frame to init outBufferSize" << endl;
        return;
      }
    } else {
      memset(outBuffer, 0, outBufferSize);
    }

    if (isNextFrameReady.load()) {
      // TODO lock/consume/unlock/notify
      // lock nextFrame
      int outDataSize = reSampler->reSample(outBuffer, outBufferSize, nextFrame);
      isNextFrameReady.store(false);
      // unlock nextFrame
      // notify

      if (outDataSize != len) {
        cout << "WARNING: outDataSize[" << outDataSize << "] != len[" << len << "]" << endl;
      }
    } else {
      // if list is empty, silent will be written.
    }
    std::memcpy(stream, outBuffer, len);
  }

  int getChannels() const {
    if (aCodecCtx != nullptr) {
      return aCodecCtx->channels;
    } else {
      throw std::runtime_error("can not getChannels.");
    }
  }

  int getChannleLayout() const {
    if (aCodecCtx != nullptr) {
      return aCodecCtx->channel_layout;
    } else {
      throw std::runtime_error("can not getChannleLayout.");
    }
  }

  int getSampleRate() const {
    if (aCodecCtx != nullptr) {
      return aCodecCtx->sample_rate;
    } else {
      throw std::runtime_error("can not getSampleRate.");
    }
  }

  int getSampleFormat() const {
    if (aCodecCtx != nullptr) {
      return (int)aCodecCtx->sample_fmt;
    } else {
      throw std::runtime_error("can not getSampleRate.");
    }
  }
};

class VideoProcessor : public MediaProcessor {
  int videoIndex = -1;
  AVCodecContext* vCodecCtx = nullptr;
  AVFrame* nextFrame = av_frame_alloc();
  AVPacket* targetPkt = nullptr;
  struct SwsContext* sws_ctx = nullptr;
  AVFrame* outPic = nullptr;

 protected:
  void prepareNextFrame() override {
    // FIXME deal with no next data.
    while (!isNextFrameReady.load() && !streamFinished) {
      if (targetPkt == nullptr) {
        auto pkt = getNextPkt();
        if (pkt == nullptr) {
          cout << "WARN: no next pkt." << endl;
          return;
        }
        targetPkt = pkt.release();
      }

      int ret = -1;
      ret = avcodec_send_packet(vCodecCtx, targetPkt);
      if (ret == 0) {
        av_packet_free(&targetPkt);
        targetPkt = nullptr;
        // cout << "[AUDIO] avcodec_send_packet success." << endl;
      } else if (ret == AVERROR(EAGAIN)) {
        // buff full, can not decode any more, nothing need to do.
        // keep the packet for next time decode.
      } else {
        string errorMsg = "[AUDIO] avcodec_send_packet error: ";
        errorMsg += ret;
        cout << errorMsg << endl;
        throw std::runtime_error(errorMsg);
      }

      ret = avcodec_receive_frame(vCodecCtx, nextFrame);
      if (ret == 0) {
        // cout << "[AUDIO] avcodec_receive_frame success." << endl;
        // success.
        isNextFrameReady.store(true);
      } else if (ret == AVERROR_EOF) {
        cout << "no more output frames." << endl;
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

 protected:
  void prepareNextFrame() override {
    // FIXME deal with no next data.
    while (!isNextFrameReady.load() && !streamFinished) {
      if (targetPkt == nullptr) {
        auto pkt = getNextPkt();
        if (pkt == nullptr) {
          cout << "WARN: no next pkt." << endl;
          return;
        }
        targetPkt = pkt.release();
      }
      int ret = -1;
      ret = avcodec_send_packet(vCodecCtx, targetPkt);
      if (ret == 0) {
        av_packet_free(&targetPkt);
        targetPkt = nullptr;
        // cout << "[VIDEO] avcodec_send_packet success." << endl;
      } else if (ret == AVERROR(EAGAIN)) {
        // buff full, can not decode any more, nothing need to do.
        // keep the packet for next time decode.
      } else {
        string errorMsg = "[VIDEO] avcodec_send_packet error: ";
        errorMsg += ret;
        cout << errorMsg << endl;
        throw std::runtime_error(errorMsg);
      }

      ret = avcodec_receive_frame(aCodecCtx, nextFrame);
      if (ret == 0) {
        // cout << "[AUDIO] avcodec_receive_frame success." << endl;
        // success.
        isNextFrameReady.store(true);
      } else if (ret == AVERROR_EOF) {
        cout << "no more output frames." << endl;
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
  VideoProcessor(AVFormatContext* formatCtx) {
    for (int i = 0; i < formatCtx->nb_streams; i++) {
      if (formatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
        videoIndex = i;
        cout << "audio stream index = : [" << i << "]" << endl;
        break;
      }
    }

    ffmpegUtil::ffUtils::initCodecContext(formatCtx, videoIndex, &vCodecCtx);

    int w = vCodecCtx->width;
    int h = vCodecCtx->height;
    auto fmt = vCodecCtx->pix_fmt;

    sws_ctx =
        sws_getContext(w, h, fmt, w, h, AV_PIX_FMT_YUV420P, SWS_BILINEAR, NULL, NULL, NULL);

    int numBytes = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, w, h, 32);
    outPic = av_frame_alloc();
    uint8_t* buffer = (uint8_t*)av_malloc(numBytes * sizeof(uint8_t));
    av_image_fill_arrays(outPic->data, outPic->linesize, buffer, AV_PIX_FMT_YUV420P, w, h, 32);
  }

  AVFrame* getFrame() { return outPic; }

  bool nextFrame() {
    if (isNextFrameReady.load()) {
      // TODO lock/consume/unlock/notify
      // lock nextFrame
      sws_scale(sws_ctx, (uint8_t const* const*)nextFrame->data, nextFrame->linesize, 0,
                vCodecCtx->height, outPic->data, outPic->linesize);
      // unlock nextFrame
      // notify
      isNextFrameReady.store(false);
      return true;
    } else {
      cout << "WARN: no frame to init outBufferSize" << endl;  // FIXME
      return false;
    }
  }

};
