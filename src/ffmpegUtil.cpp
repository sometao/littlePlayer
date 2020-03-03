#ifdef _WIN32
// Windows
extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libswresample/swresample.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
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
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#ifdef __cplusplus
};
#endif
#endif
#include <iostream>

#include <string>
#include <fstream>

using std::string;

namespace ffmpegUtil {

void writeY420pData(std::ofstream& os, unsigned char* data, int linesize, int width, int height) {
  char* p = reinterpret_cast<char*>(data);
  for (int i = 0; i < height; i++) {
    os.write(p + (__int64)i * linesize, width);
  }
}

void writeY420pFrame(std::ofstream& os, AVFrame* frame) {
  int width = frame->width;
  int height = frame->height;
  writeY420pData(os, frame->data[0], frame->linesize[0], width, height);
  writeY420pData(os, frame->data[1], frame->linesize[1], width / 2, height / 2);
  writeY420pData(os, frame->data[2], frame->linesize[2], width / 2, height / 2);
}

void writeY420pFrame2Buffer(char* buffer, AVFrame* frame) {
  int width = frame->width;
  int height = frame->height;
  char* dst = buffer;

  char* src;
  int linesize;
  int xSize;
  int ySize;

  //------------------

  src = reinterpret_cast<char*>(frame->data[0]);
  linesize = frame->linesize[0];
  xSize = width;
  ySize = height;

  for (int i = 0; i < ySize; i++) {
    std::memcpy(dst, src + (__int64)i * linesize, xSize);
    dst += xSize;
  }

  //------------------

  src = reinterpret_cast<char*>(frame->data[1]);
  linesize = frame->linesize[1];
  xSize = width / 2;
  ySize = height / 2;

  for (int i = 0; i < ySize; i++) {
    std::memcpy(dst, src + (__int64)i * linesize, xSize);
    dst += xSize;
  }

  //------------------

  src = reinterpret_cast<char*>(frame->data[2]);
  linesize = frame->linesize[2];
  xSize = width / 2;
  ySize = height / 2;

  for (int i = 0; i < ySize; i++) {
    std::memcpy(dst, src + (__int64)i * linesize, xSize);
    dst += xSize;
  }
}

/**
 * Resample the audio data retrieved using FFmpeg before playing it.
 *
 * @param   audio_decode_ctx    the audio codec context retrieved from the original AVFormatContext.
 * @param   decoded_audio_frame the decoded audio frame.
 * @param   out_sample_fmt      audio output sample format (e.g. AV_SAMPLE_FMT_S16).
 * @param   out_channels        audio output channels, retrieved from the original audio codec
 * context.
 * @param   out_sample_rate     audio output sample rate, retrieved from the original audio codec
 * context.
 * @param   out_buf             audio output buffer.
 *
 * @return                      the size of the resampled audio data.
 */
int audio_resampling(  // 1
    AVCodecContext* audio_decode_ctx, AVFrame* decoded_audio_frame,
    enum AVSampleFormat out_sample_fmt, int out_channels, int out_sample_rate, uint8_t* out_buf) {
  // check global quit flag

  SwrContext* swr_ctx = NULL;
  int ret = 0;
  int64_t in_channel_layout = audio_decode_ctx->channel_layout;
  int64_t out_channel_layout = AV_CH_LAYOUT_STEREO;
  int out_nb_channels = 0;
  int out_linesize = 0;
  int in_nb_samples = 0;
  int out_nb_samples = 0;
  int max_out_nb_samples = 0;
  uint8_t** resampled_data = NULL;
  int resampled_data_size = 0;

  swr_ctx = swr_alloc();

  if (!swr_ctx) {
    printf("swr_alloc error.\n");
    return -1;
  }

  // get input audio channels
  in_channel_layout = (audio_decode_ctx->channels ==
                       av_get_channel_layout_nb_channels(audio_decode_ctx->channel_layout))
                          ?  // 2
                          audio_decode_ctx->channel_layout
                          : av_get_default_channel_layout(audio_decode_ctx->channels);

  // check input audio channels correctly retrieved
  if (in_channel_layout <= 0) {
    printf("in_channel_layout error.\n");
    return -1;
  }

  // set output audio channels based on the input audio channels
  if (out_channels == 1) {
    out_channel_layout = AV_CH_LAYOUT_MONO;
  } else if (out_channels == 2) {
    out_channel_layout = AV_CH_LAYOUT_STEREO;
  } else {
    out_channel_layout = AV_CH_LAYOUT_SURROUND;
  }

  // retrieve number of audio samples (per channel)
  in_nb_samples = decoded_audio_frame->nb_samples;
  if (in_nb_samples <= 0) {
    printf("in_nb_samples error.\n");
    return -1;
  }

  // Set SwrContext parameters for resampling
  av_opt_set_int(  // 3
      swr_ctx, "in_channel_layout", in_channel_layout, 0);

  // Set SwrContext parameters for resampling
  av_opt_set_int(swr_ctx, "in_sample_rate", audio_decode_ctx->sample_rate, 0);

  // Set SwrContext parameters for resampling
  av_opt_set_sample_fmt(swr_ctx, "in_sample_fmt", audio_decode_ctx->sample_fmt, 0);

  // Set SwrContext parameters for resampling
  av_opt_set_int(swr_ctx, "out_channel_layout", out_channel_layout, 0);

  // Set SwrContext parameters for resampling
  av_opt_set_int(swr_ctx, "out_sample_rate", out_sample_rate, 0);

  // Set SwrContext parameters for resampling
  av_opt_set_sample_fmt(swr_ctx, "out_sample_fmt", out_sample_fmt, 0);

  // Once all values have been set for the SwrContext, it must be initialized
  // with swr_init().
  ret = swr_init(swr_ctx);
  ;
  if (ret < 0) {
    printf("Failed to initialize the resampling context.\n");
    return -1;
  }

  max_out_nb_samples = out_nb_samples =
      av_rescale_rnd(in_nb_samples, out_sample_rate, audio_decode_ctx->sample_rate, AV_ROUND_UP);

  // check rescaling was successful
  if (max_out_nb_samples <= 0) {
    printf("av_rescale_rnd error.\n");
    return -1;
  }

  // get number of output audio channels
  out_nb_channels = av_get_channel_layout_nb_channels(out_channel_layout);

  ret = av_samples_alloc_array_and_samples(&resampled_data, &out_linesize, out_nb_channels,
                                           out_nb_samples, out_sample_fmt, 0);

  if (ret < 0) {
    printf("av_samples_alloc_array_and_samples() error: Could not allocate destination samples.\n");
    return -1;
  }

  // retrieve output samples number taking into account the progressive delay
  out_nb_samples =
      av_rescale_rnd(swr_get_delay(swr_ctx, audio_decode_ctx->sample_rate) + in_nb_samples,
                     out_sample_rate, audio_decode_ctx->sample_rate, AV_ROUND_UP);

  // check output samples number was correctly retrieved
  if (out_nb_samples <= 0) {
    printf("av_rescale_rnd error\n");
    return -1;
  }

  if (out_nb_samples > max_out_nb_samples) {
    // free memory block and set pointer to NULL
    av_free(resampled_data[0]);

    // Allocate a samples buffer for out_nb_samples samples
    ret = av_samples_alloc(resampled_data, &out_linesize, out_nb_channels, out_nb_samples,
                           out_sample_fmt, 1);

    // check samples buffer correctly allocated
    if (ret < 0) {
      printf("av_samples_alloc failed.\n");
      return -1;
    }

    max_out_nb_samples = out_nb_samples;
  }

  if (swr_ctx) {
    // do the actual audio data resampling
    ret = swr_convert(swr_ctx, resampled_data, out_nb_samples,
                      (const uint8_t**)decoded_audio_frame->data, decoded_audio_frame->nb_samples);

    // check audio conversion was successful
    if (ret < 0) {
      printf("swr_convert_error.\n");
      return -1;
    }

    // Get the required buffer size for the given audio parameters
    resampled_data_size =
        av_samples_get_buffer_size(&out_linesize, out_nb_channels, ret, out_sample_fmt, 1);

    // check audio buffer size
    if (resampled_data_size < 0) {
      printf("av_samples_get_buffer_size error.\n");
      return -1;
    }
  } else {
    printf("swr_ctx null error.\n");
    return -1;
  }

  // copy the resampled data to the output buffer
  memcpy(out_buf, resampled_data[0], resampled_data_size);

  /*
   * Memory Cleanup.
   */
  if (resampled_data) {
    // free memory block and set pointer to NULL
    av_freep(&resampled_data[0]);
  }

  av_freep(&resampled_data);
  resampled_data = NULL;

  if (swr_ctx) {
    // Free the given SwrContext and set the pointer to NULL
    swr_free(&swr_ctx);
  }

  return resampled_data_size;
}

}  // namespace ffmpegUtil