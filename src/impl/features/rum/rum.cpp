#include "rum.hpp"

namespace datadog::impl {

Rum::Rum() {
  // No-op constructor - this is a "do-nothing" API implementation
}

std::optional<Report> Rum::UploadThread_PrepareReport(
    const CoreContext& context, BatchReader& reader
) {
  (void)context;
  (void)reader;
  // No events to process yet - return nullopt to indicate no report
  return std::nullopt;
}

}  // namespace datadog::impl
