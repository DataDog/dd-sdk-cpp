#pragma once

#include <string>
#include <string_view>

#include "core/feature.hpp"

namespace datadog::impl {

/**
 * RUM feature implementation. Provides Real User Monitoring functionality
 * for tracking sessions, views, and actions in C++ applications.
 */
class Rum final : public Feature {
 public:
  explicit Rum();

  FeatureId GetId() const override { return CreateFeatureId("RUMS"); }

  std::string_view GetName() const override { return "rum"; }

  std::optional<Report> UploadThread_PrepareReport(
      const CoreContext& context, BatchReader& reader
  ) override;

 private:
  // No functionality implemented yet - this is a "do-nothing" API
};

}  // namespace datadog::impl
