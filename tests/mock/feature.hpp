// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "datadog/impl/core/core.hpp"
#include "datadog/impl/core/feature_read.hpp"
#include "datadog/impl/core/writer.hpp"

using namespace datadog;

/**
 * Persistent copy of a TLV Block read from a file by a MockFeature.
 */
struct TLVBlockCopy {
  impl::TLVBlockType type;
  std::string data;

  TLVBlockCopy(impl::TLVBlockType in_type, std::string_view in_data)
      : type(in_type), data(in_data) {}
};

/**
 * Description of a single call made to UploadThread_PrepareReport in MockFeature.
 */
struct MockReport {
  std::string url;
  std::string headers;
  std::string body;

  std::vector<TLVBlockCopy> blocks_read;
  std::optional<impl::BatchReader::Result::Status> last_read_status;
};

/**
 * Mock Feature implementation. Can be registered, can produce events using the callback
 * provided via OnCoreStarted(), can prepare reports from TLV batches; with all relevant
 * operations recorded for examination.
 */
class MockFeature : public impl::Feature {
 public:
  // Store feature details for easy initialization/registration
  impl::FeatureId id;
  std::string name;

  // Test can set these values manually after ctor
  std::string path{"/api/v1/test"};
  std::string content_type{"text/plain"};

  // Calls to start/stop will be recorded
  int num_start_calls{0};
  int num_stop_calls{0};

  // Each report generated will be recorded here for tests to examine
  std::vector<MockReport> reports;

  MockFeature(impl::FeatureId in_id, std::string_view in_name)
      : id(in_id), name(in_name) {}

  // Implement the basic interface used by Core

  impl::FeatureId GetId() const override { return id; }

  virtual std::string_view GetName() const override { return name; }

  virtual void Start() override { num_start_calls++; }

  virtual void Stop() override { num_stop_calls++; }

  /**
   * Allows tests to arbitrarily generate events.
   */
  bool GenerateEvent(impl::Block event, impl::Block event_metadata = {}) {
    if (!_scope) return false;
    std::string event_data(event.data(), event.size());
    std::string metadata(event_metadata.data(), event_metadata.size());
    _scope->ExecuteOnContextThread([event_data, metadata](
                                       const impl::CoreContext&,
                                       const impl::EventWriter& writer,
                                       const impl::MessagePublisher&
                                   ) {
      const bool bypass_tracking_consent = false;
      writer(event_data, metadata, bypass_tracking_consent);
    });
    return true;
  }

  /**
   * Default implementation to record reports generated under test.
   */
  virtual std::optional<impl::Report> UploadThread_PrepareReport(
      impl::BatchReader& reader, impl::RequestBuilder& builder
  ) override {
    // Use a report struct to contain all the relevant data about this call
    MockReport report;

    // Build URL and headers based on configuration
    builder.Reset(path, content_type);
    builder.AddQueryParam_ddsource();

    // Copy URL and headers into MockReport
    report.url.assign(builder.GetUrl());
    report.headers.assign(builder.GetHeaders());

    // Read all TLV blocks into a vector, serially
    bool read_ok = true;
    while (true) {
      // Store the result of the most recent read
      auto result = reader.ReadNext();
      report.last_read_status = result.status;

      // If read failed, abort
      if (result.status == impl::BatchReader::Result::Status::Error) {
        read_ok = false;
        break;
      }

      // If the BatchReader has reached EOF, we're done: no more blocks to read
      if (result.status == impl::BatchReader::Result::Status::EndOfFile) {
        break;
      }

      // We successfully read a full block; copy its data to vector
      DATADOG_ASSERT(result.status == impl::BatchReader::Result::Status::Success, "");
      report.blocks_read.emplace_back(result.block.type, result.block.data);
    }

    // If any reads failed, produce no report and early-out
    if (!read_ok) {
      return std::nullopt;
    }

    // Build full HTTP request body sychronously
    report.body = BuildRequestBody(report.blocks_read);

    // Move report into our vector, both so tests can examine it, and so our HTTP client
    // can reference the request body beyond the lifetime of this function
    const MockReport& stored = reports.emplace_back(std::move(report));

    // Return the resulting report
    return impl::Report{
        stored.url.c_str(), stored.headers.c_str(), impl::StringWriter{stored.body}
    };
  }

  // Override to implement custom block processing
  virtual std::string BuildRequestBody(const std::vector<TLVBlockCopy>& blocks) {
    // By default, ignore metadata blocks and join all event blocks with ','
    std::ostringstream oss;
    int num_events_seen = 0;
    for (const TLVBlockCopy& block : blocks) {
      // If not an event, skip it
      if (block.type != impl::TLVBlockType::Event) {
        continue;
      }

      // Prepend comma for all events after the first, then add raw event data
      if (num_events_seen > 0) {
        oss << ',';
      }
      oss << block.data;
      num_events_seen++;
    }
    return oss.str();
  }
};
