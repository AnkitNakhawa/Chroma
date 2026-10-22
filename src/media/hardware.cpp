#include "chroma/media/hardware.hpp"
#include "../core/parallel.hpp"

#include <string>

#include <nlohmann/json.hpp>

#ifndef CHROMA_HAS_FFMPEG
#define CHROMA_HAS_FFMPEG 0
#endif

#if CHROMA_HAS_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
#include <libavutil/hwcontext.h>
}
#endif

namespace chroma {
namespace {

#if CHROMA_HAS_FFMPEG
std::string ffmpeg_error(int status) {
  char buffer[AV_ERROR_MAX_STRING_SIZE]{};
  if (av_strerror(status, buffer, sizeof(buffer)) < 0) {
    return "unknown FFmpeg error";
  }
  return buffer;
}

bool decoder_supports_device(const AVCodec* decoder, AVHWDeviceType device_type) noexcept {
  if (decoder == nullptr) {
    return false;
  }
  for (int index = 0;; ++index) {
    const auto* config = avcodec_get_hw_config(decoder, index);
    if (config == nullptr) {
      return false;
    }
    if (config->device_type == device_type &&
        (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) != 0) {
      return true;
    }
  }
}
#endif

}  // namespace

HardwarePipelineCapabilities query_hardware_pipeline_capabilities() {
  HardwarePipelineCapabilities capabilities;
#if CHROMA_HAS_FFMPEG
  capabilities.ffmpeg_available = true;
  const auto cuda_type = av_hwdevice_find_type_by_name("cuda");
  if (cuda_type == AV_HWDEVICE_TYPE_NONE) {
    capabilities.detail = "FFmpeg does not expose the CUDA hardware-device type";
    return capabilities;
  }
  capabilities.cuda_device_type_available = true;
  capabilities.h264_hardware_decode_available =
      decoder_supports_device(avcodec_find_decoder(AV_CODEC_ID_H264), cuda_type);
  capabilities.h264_nvenc_available = avcodec_find_encoder_by_name("h264_nvenc") != nullptr;

  AVBufferRef* device = nullptr;
  const auto status = av_hwdevice_ctx_create(&device, cuda_type, nullptr, nullptr, 0);
  if (status >= 0) {
    capabilities.cuda_device_creatable = true;
    capabilities.detail =
        "CUDA device creation succeeded; Chroma's hardware-frame pipeline is not implemented";
  } else {
    capabilities.detail = "CUDA device creation failed: " + ffmpeg_error(status);
  }
  av_buffer_unref(&device);
#else
  capabilities.detail = "Chroma was built without FFmpeg media support";
#endif
  return capabilities;
}

std::string hardware_pipeline_capabilities_json(
    const HardwarePipelineCapabilities& capabilities) {
  return nlohmann::json{
      {"configured_cpu_thread_limit", detail::cpu_thread_limit()},
      {"ffmpeg_available", capabilities.ffmpeg_available},
      {"cuda_device_type_available", capabilities.cuda_device_type_available},
      {"cuda_device_creatable", capabilities.cuda_device_creatable},
      {"h264_hardware_decode_available", capabilities.h264_hardware_decode_available},
      {"h264_nvenc_available", capabilities.h264_nvenc_available},
      {"pipeline_implemented", capabilities.pipeline_implemented},
      {"detail", capabilities.detail}}
      .dump(2);
}

}  // namespace chroma
