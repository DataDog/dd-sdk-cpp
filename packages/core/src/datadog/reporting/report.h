// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2024-Present
// Datadog, Inc.
#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace datadog::core::reporting {

class Report {
 public:
  struct HeaderKeys {
    static constexpr std::string_view kContentType{"Content-Type"};
    static constexpr std::string_view kContentEncoding{"Content-Encoding"};
    static constexpr std::string_view kUserAgent{"User-Agent"};
    static constexpr std::string_view kDdApiKey{"DD-API-KEY"};
    static constexpr std::string_view kDdEvpOrigin{"DD-EVP-ORIGIN"};
    static constexpr std::string_view kDdEvpOriginVersion{
        "DD-EVP-ORIGIN-VERSION"};
    static constexpr std::string_view kDdRequestId{"DD-REQUEST-ID"};
    static constexpr std::string_view kDdIdempotencyKey{"DD-IDEMPOTENCY-KEY"};
  };

  using Headers = std::unordered_map<std::string, std::string>;
  using Query = std::vector<std::string>;

  Report() {}
  explicit Report(std::string_view path) : path_(path) {}
  ~Report() = default;
  Report(Report&&) = default;
  Report& operator=(Report&&) = default;
  // Copy here is disallowed because it's too expensive and likely a mistake
  Report(const Report&) = delete;
  Report& operator=(const Report&) = delete;

  const std::string_view& GetPath() const { return path_; }

  const Headers& GetHeaders() const { return headers_; }
  void SetHeader(std::string_view header, std::string_view value) {
    headers_.emplace(header, value);
  }

  void AddQuery(std::string_view query) {
    query_.push_back(std::string(query));
  }
  const Query& GetQuery() const { return query_; }

  void SetBody(std::string&& body) { body_ = body; }
  const std::string& GetBody() const { return body_; }

 private:
  std::string_view path_;
  Headers headers_;
  Query query_;
  std::string body_;
};

}  // namespace datadog::core::reporting
