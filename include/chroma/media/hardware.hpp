#pragma once

#include <string>

namespace chroma {

struct HardwarePipelineCapabilities {
  bool ffmpeg_available{false};
  bool cuda_device_type_available{false};
  bool cuda_device_creatable{false};
  bool h264_hardware_decode_available{false};
  bool h264_nvenc_available{false};
  bool pipeline_implemented{false};
  std::string detail;
};

[[nodiscard]] HardwarePipelineCapabilities query_hardware_pipeline_capabilities();
[[nodiscard]] std::string hardware_pipeline_capabilities_json(
    const HardwarePipelineCapabilities& capabilities);

}  // namespace chroma
