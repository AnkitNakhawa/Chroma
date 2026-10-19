#pragma once

#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

#include "chroma/core/composition.hpp"

namespace chroma {

struct PreflightDiagnostic {
  std::string severity;
  std::string code;
  std::string path;
  std::string message;
  std::string asset_id;
  std::string asset_path;
};

struct PreflightReport {
  std::vector<PreflightDiagnostic> diagnostics;
  std::size_t assets_checked{0};
  [[nodiscard]] bool ready() const noexcept;
};

struct PreflightOptions {
  bool for_video{true};
  std::function<bool()> cancelled;
};

class PreflightError : public std::runtime_error {
 public:
  explicit PreflightError(PreflightReport report);
  [[nodiscard]] const PreflightReport& report() const noexcept { return report_; }
 private:
  PreflightReport report_;
};

[[nodiscard]] PreflightReport preflight_inputs(const Composition& composition,
                                               const PreflightOptions& options = {});
[[nodiscard]] std::string preflight_report_json(const PreflightReport& report);

}  // namespace chroma
