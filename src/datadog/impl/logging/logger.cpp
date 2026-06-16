// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/logging/logger.hpp"

#include "datadog/impl/core/attribute/merge.hpp"
#include "datadog/impl/core/feature_types/logging.hpp"
#include "datadog/impl/core/platform/system_info.hpp"
#include "datadog/impl/core/util/assert.hpp"
#include "datadog/impl/core/util/diagnostics.hpp"
#include "datadog/impl/logging/event_generation.hpp"
#include "datadog/impl/logging/logging.hpp"

namespace datadog::impl {

Logger::Logger(const LoggerConfig& config, std::weak_ptr<Logging> logging)
    : _logging(std::move(logging)),
      _attributes(Attribute::Object(config.initial_attribute_capacity)),
      _details(
          std::make_shared<const LoggerConfigDetails>(LoggerConfigDetails{
              config.service.value_or(""),
              config.name.value_or(""),
              config.enrich_with_rum_context
          })
      ),
      _remote_log_threshold(config.remote_log_threshold),
      _remote_sample_rate_unit(config.remote_sample_rate / 100.0f),
      _sampling_rng(std::random_device{}()),
      _sampling_distribution(0.0f, 1.0f) {}

void Logger::AddAttribute(std::string_view key, const Attribute& value) {
  // No mutex protects logger-level attributes; Logger should only be used on a single
  // thread at any given time
  _attributes.SetObjectProperty(key, value);
}

void Logger::RemoveAttribute(std::string_view key) {
  // No mutex protects logger-level attributes; Logger should only be used on a single
  // thread at any given time
  _attributes.DeleteObjectProperty(key);
}

void Logger::AddTag(std::string_view tag, const DiagnosticLogger& diagnostic_logger) {
  // Add a pre-':'-concatenated tag entry to our set of logger tags
  const auto res = _tags.AddEntry(tag);
  HandleAddTagResult(res, tag.substr(0, tag.find(':')), diagnostic_logger);
}

void Logger::AddTag(
    std::string_view key,
    std::string_view value,
    const DiagnosticLogger& diagnostic_logger
) {
  // Add a new '<key>:<value>' entry formed from the given values
  const auto res = _tags.Add(key, value);
  HandleAddTagResult(res, key, diagnostic_logger);
}

void Logger::RemoveTag(std::string_view tag) {
  // Remove any entry that was previously added with this exact value
  _tags.RemoveEntry(tag);
}

void Logger::RemoveTagsWithKey(std::string_view key) {
  // Remove any and all entries prefixed with this key
  _tags.RemoveEntriesWithKey(key);
}

void Logger::Log(
    LogLevel level,
    std::string_view message,
    const LogError& err,
    const Attribute& attributes
) const {
  // Lock our weak_ptr to ensure that the Logging feature implementation is still alive
  // and will remain so for the duration of this call
  if (auto logging = _logging.lock()) {
    // Verify that the FeatureScope still exists: if null, the SDK is no longer running
    if (!logging->_scope) {
      return;
    }
    FeatureScope& scope = *logging->_scope;

    // Make a sampling decision on the calling thread
    if (!ShouldSendLogEvent(level)) {
      return;
    }

    // Start preparing the data that ContextThread_GenerateLogEvent will need in order
    // to produce a LogEvent, starting with logger-specific configuration details
    std::shared_ptr<const LoggerConfigDetails> logger_details = _details;

    // Copy any error details provided with this call
    std::string error_message{err.message};
    std::string error_kind{err.kind};
    std::string error_stack{err.stack};

    // Next, copy the message and all related message-specific details so it can be
    // passed to the context thread
    LogCallDetails log_call{
        level,
        std::string{message},
        std::move(error_message),
        std::move(error_kind),
        std::move(error_stack),
        logging->_clock.Now(),
        Attribute::Object(),
        std::string{_tags.Get()}
    };

    // Grab a snapshot of the global logging attributes set for the feature, then merge
    // in logger-level and message-level attributes (note that while access to Logging's
    // set of global attributes is protected with a shared_mutex, there is no
    // synchronization on logger-level attributes, since Loggers are assumed not to be
    // used concurrently from separate threads)
    Attribute global_attributes = logging->SnapshotGlobalAttributes();
    AttributeMerge::AssembleObject(
        log_call.merged_attributes, {global_attributes, _attributes, attributes}
    );

    // Grab a reference to the shared buffer used to encode all log messages: it's safe
    // for all Loggers to use this value in their context-thread routines, since all
    // context-thread work is executed serially. The buffer is owned by impl::Logging,
    // which impl::Core keeps alive via shared_ptr for as long as the context thread is
    // running.
    std::vector<uint8_t>& encode_buf = logging->_encode_buffer;

    // Capture logger and log call details, as well as the encode buffer reference, and
    // enqueue `ContextThread_GenerateLogEvent` to be called on the context thread
    scope.ExecuteOnContextThread(
        [logger = std::move(logger_details), call = std::move(log_call), &encode_buf](
            const CoreContext& ctx,
            const EventWriter& event_writer,
            const MessagePublisher& publisher
        ) mutable {
          // Use a mutable lambda and std::move the LogCallDetails so we can pass
          // ownership of our std::string copy of the message, rather than creating
          // another copy
          ContextThread_GenerateLogEvent(
              *logger, std::move(call), ctx, event_writer, encode_buf, publisher
          );
        }
    );
  }
}

void Logger::HandleAddTagResult(
    LoggerTags::AddResult res,
    std::string_view input_key,
    const DiagnosticLogger& diagnostic_logger
) {
  switch (res) {
    case LoggerTags::AddResult::Accepted:
      break;
    case LoggerTags::AddResult::AcceptedWithTruncation:
      diagnostic_logger.Warning(
          "Logger tag was truncated due to excessive length: tags must not exceed 200 "
          "bytes in length",
          {{"key", input_key}}
      );
      break;
    case LoggerTags::AddResult::AcceptedWithSanitization:
      diagnostic_logger.Warning(
          "Logger tag was modified to conform to required format rules: tags must be "
          "lowercase alphanumeric and contain no special characters other than [_:./-]",
          {{"key", input_key}}
      );
      break;
    case LoggerTags::AddResult::RejectedAsEmpty:
      diagnostic_logger.Error(
          "Logger tag was rejected: tags must not be empty", {{"key", input_key}}
      );
      break;
    case LoggerTags::AddResult::RejectedAsNotBeginningAZ:
      diagnostic_logger.Error(
          "Logger tag was rejected: tags must begin with a letter", {{"key", input_key}}
      );
      break;
    case LoggerTags::AddResult::RejectedAsReserved:
      diagnostic_logger.Error(
          "Logger tag was rejected: tags must not conflict with reserved values",
          {{"key", input_key}}
      );
      break;
    case LoggerTags::AddResult::RejectedDueToTagLimit:
      diagnostic_logger.Error(
          "Logger tag was rejected: loggers may not have more than 100 tags",
          {{"key", input_key}}
      );
      break;
  }
}

bool Logger::ShouldSendLogEvent(LogLevel level) const {
  // If the log message is being recorded at a severity level that's below the minimum
  // threshold for remote logging, send no remote log event
  if (level < _remote_log_threshold) {
    return false;
  }

  // If our sampling rate is zero, never send any messages
  if (_remote_sample_rate_unit <= 0.0f) {
    return false;
  }

  // If our sampling rate is 100%, always send message that meet our threshold; don't
  // bother with RNG
  if (_remote_sample_rate_unit >= 1.0f) {
    return true;
  }

  // Sampling rate is nonzero but below 100%: sample our logger's RNG to see if we meet
  // the threshold
  const float random_unit_value = _sampling_distribution(_sampling_rng);

  // Sampling rate defines percentage of messages to _keep_ (e.g. 0.9f => 90% sampled)
  return random_unit_value <= _remote_sample_rate_unit;
}

}  // namespace datadog::impl
