// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2024-Present
// Datadog, Inc.
#include "datadog/reporting/libcurl_reporter.h"

#include <sstream>

namespace datadog::core::reporting {

DatadogReporter::Status TranslateLibCurlCode(CURLcode code, CURL* curl) {
  DatadogReporter::Status status = DatadogReporter::Status::UnrecoverableError;
  switch (code) {
    case CURLE_OK: {
      int http_code = 0;
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
      curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
      status = DatadogReporter::FromHttpStatusCode(http_code);
      break;
    }
    // These are all possibly recoverable
    case CURLE_COULDNT_RESOLVE_PROXY:
    case CURLE_COULDNT_RESOLVE_HOST:
    case CURLE_COULDNT_CONNECT:
    case CURLE_WEIRD_SERVER_REPLY:
    case CURLE_REMOTE_ACCESS_DENIED:
    case CURLE_HTTP2:
    case CURLE_PARTIAL_FILE:
    case CURLE_UPLOAD_FAILED:
    case CURLE_OPERATION_TIMEDOUT:
    case CURLE_SSL_CONNECT_ERROR:
    case CURLE_TOO_MANY_REDIRECTS:
    case CURLE_GOT_NOTHING:
    case CURLE_SEND_ERROR:
    case CURLE_RECV_ERROR:
    case CURLE_PEER_FAILED_VERIFICATION:
    case CURLE_HTTP2_STREAM:
    case CURLE_PROXY:
    case CURLE_HTTP3:
      status = DatadogReporter::Status::ErrorNeedsRetry;
      break;
    default:
      status = DatadogReporter::Status::UnrecoverableError;
      break;
  }

  return status;
}

std::unique_ptr<DatadogReporter> LibcurlReporter::Create(
    std::string_view host) {
  auto reporter = std::make_unique<LibcurlReporter>(host);
  reporter->Init();
  return reporter;
}

void LibcurlReporter::Init() {
  // TODO: curl_global_cleanup -- need to ensure cleanup happens only once?
  curl_global_init(CURL_GLOBAL_ALL);
}

DatadogReporter::Status LibcurlReporter::Send(const Report& report) {
  CURL* curl{nullptr};
  CURLU* urlp{nullptr};
  curl_slist* header_list{nullptr};

  // If we don't get to the end, we've hit an unrecoverable error
  DatadogReporter::Status status = DatadogReporter::Status::UnrecoverableError;

  try {
    curl = curl_easy_init();
    if (!curl) {
      // TELEM: Couldn't get curl
      return DatadogReporter::Status::UnrecoverableError;
    }

    // Use of goto here simplifies cleanup. It is safe to goto out of a try
    // block
    // NOLINTBEGIN(cppcoreguidelines-avoid-goto,cppcoreguidelines-pro-type-vararg)
    if (!(urlp = AssembleUrl(report))) {
      goto fail;
    }
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_CURLU, urlp)) {
      goto fail;
    }

    const auto& query = report.GetQuery();
    for (const auto& query_part : query) {
      curl_url_set(urlp, CURLUPART_QUERY, query_part.c_str(),
                   CURLU_APPENDQUERY);
    }

    header_list = nullptr;
    for (const auto& header : report.GetHeaders()) {
      // Not all header sets are failure. If a header is set in a way that
      // intake doesn't like, it will return a failure in the
      // TODO(jeff.ward): Prevent this extra string allocation
      std::string full_header =
          (std::stringstream() << header.first << ": " << header.second).str();
      header_list = curl_slist_append(header_list, full_header.c_str());
    }
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, report.GetBody().c_str());

    CURLcode code = curl_easy_perform(curl);
    status = TranslateLibCurlCode(code, curl);
  } catch (...) {
    // TELEM: Report curl failure
  }

fail:
  // NOLINTEND(cppcoreguidelines-avoid-goto,cppcoreguidelines-pro-type-vararg)
  if (urlp) {
    curl_url_cleanup(urlp);
  }
  if (header_list) {
    curl_slist_free_all(header_list);
  }
  if (curl) {
    curl_easy_cleanup(curl);
  }

  return status;
}

CURLU* LibcurlReporter::AssembleUrl(const Report& report) {
  CURLU* urlp = curl_url();
  if (!urlp) {
    return nullptr;
  }
  if (CURLUE_OK != curl_url_set(urlp, CURLUPART_URL, host_.c_str(), 0)) {
    curl_url_cleanup(urlp);
    return nullptr;
  }
  if (CURLUE_OK !=
      curl_url_set(urlp, CURLUPART_PATH, report.GetPath().data(), 0)) {
    curl_url_cleanup(urlp);
    return nullptr;
  }

  return urlp;
}

}  // namespace datadog::core::reporting
