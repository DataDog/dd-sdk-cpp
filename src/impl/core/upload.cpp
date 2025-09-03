#include "core/upload.hpp"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <iostream>
#include <queue>
#include <string>
#include <thread>

#include "core/block.hpp"
#include "core/core.hpp"
#include "platform/filesystem.hpp"

namespace datadog::impl {

static const platform::Duration ASSERTION_FAILURE_BACKOFF = from_seconds(60.0);

enum class _process_and_upload_batch_result : uint8_t
{
    /**
     * Request was successful; upload thread should delete the file and continue the
     * current upload cycle.
     */
    success,
    /**
     * Request failed due to transient network conditions that may improve in the
     * future; upload thread should retain the file, abort the current upload cycle, and
     * increase backoff delay.
     */
    retryable_failure,
    /**
     * Request failed because the batch itself is malformed, the feature implementation
     * was unable to process it, the resulting HTTP request was malformed, or the remote
     * server rejected the batch; upload thread should delete the file and continue the
     * current upload cycle.
     */
    bad_batch
};

static _process_and_upload_batch_result _interpret_http_result(platform::HttpResult res)
{
    switch (res.type)
    {
        // If we couldn't even attempt the request, or if we failed to get a response
        // for a reason that indicates an inherent problem with the request, rather than
        // transient network conditions etc., consider the batch file a poison pill and
        // delete it
        case platform::HttpResultType::SentNoRequest:
        case platform::HttpResultType::GotNoResponse_NonRetryable:
            return _process_and_upload_batch_result::bad_batch;

        // If we failed to complete the request due to transient network conditions,
        // keep the batch file around, but don't process any more batches for now
        case platform::HttpResultType::GotNoResponse_Retryable:
            return _process_and_upload_batch_result::retryable_failure;

        // If we got a valid HTTP response, discriminate based on the status code
        case platform::HttpResultType::GotResponse:
            // Any 200-level response indicates success
            if (res.status_code >= 200 && res.status_code <= 299)
            {
                return _process_and_upload_batch_result::success;
            }

            // Treat some client errors and most common server errors as retryable
            switch (res.status_code)
            {
                case 408: // Request Timeout
                case 429: // Too Many Requests
                case 500: // Internal Server Error
                case 502: // Bad Gateway
                case 503: // Service Unavailable
                case 504: // Gateway Timeout
                case 507: // Insufficient Storage
                    return _process_and_upload_batch_result::retryable_failure;

                // For all other status codes, break
                default:
                    break;
            }

            // Treat all other responses as inherent flaws in the request payload itself
            return _process_and_upload_batch_result::bad_batch;
    }
}

static _process_and_upload_batch_result _process_and_upload_batch(
    const CoreContext& core_context,
    Feature& feature_impl,
    platform::IDirectory& directory,
    platform::IHttpClient& http_client,
    std::string_view filename,
    std::vector<char>& mut_read_buffer
)
{
    // This file is ready to process: attempt to open it for read
    auto open_result = directory.OpenForRead(filename);
    if (!open_result)
    {
        // If we failed to open the file for any reason, leave it in place and
        // continue to the next file
        return _process_and_upload_batch_result::retryable_failure;
    }
    platform::IFileReader& file = *open_result->get();

    // Initialize a BatchReader, which the feature implementation will use to
    // iteratively process each TLV block stored in the file
    BatchReader batch_reader(file, mut_read_buffer);
    auto report = feature_impl.UploadThread_PrepareReport(core_context, batch_reader);
    if (!report)
    {
        // If the feature elected not to generate a report from this batch, or was
        // unable to process it, delete the file and continue processing
        return _process_and_upload_batch_result::bad_batch;
    }

    // The report defines a POST endpoint and the HTTP headers that should be set on the
    // request, along with a body_writer function that our HTTP client will use to
    // populate the request body with chunked encoding: this allows us to stream data
    // from the batch file directly to the socket, without intermediate copies. As a
    // result, our open file handle MUST remain in scope until the HTTP request is
    // finished.

    // Initiate an HTTP request, blocking until it finishes
    std::cout << "<UPLOAD> POST " << report->url << "\n";
    const platform::HttpResult res =
        http_client.Post(report->url, report->headers, report->body_writer);
    std::cout << "<UPLOAD> Result type " << static_cast<int>(res.type) << ", status "
              << res.status_code << "\n";

    // Interpret the result of our HTTP request and return the intended action, closing
    // the file in the process
    return _interpret_http_result(res);
}

static platform::Duration _run_upload_cycle( // NOLINT(readability-function-cognitive-complexity)
    UploadThreadConfig config,
    const CoreContext& core_context,
    const platform::IClock& clock,
    RegisteredFeature& feature,
    platform::IHttpClient& http_client,
    std::vector<std::string>& mut_filenames,
    std::vector<char>& mut_read_buffer
)
{

    // The feature should have its own state related to upload attempt timing etc.
    if (!feature.upload_state)
    {
        assert(false && "registered feature has no upload state in upload thread");
        return ASSERTION_FAILURE_BACKOFF;
    }

    // The storage thread maintains a separate subdirectory for events that we have the
    // user's consent to upload: a wrapper for that directory should have been
    // initialized when the feature was registered
    if (!feature.event_read_directory)
    {
        assert(false && "registered feature has no read directory in upload thread");
        return ASSERTION_FAILURE_BACKOFF;
    }
    platform::IDirectory& directory = *feature.event_read_directory;

    // Retrieve a list of all filenames in the relevant directory
    mut_filenames.clear();
    if (!directory.ListFiles(mut_filenames))
    {
        return feature.upload_state->current_delay;
    }

    // Sort all filenames lexicographically: this will cause an ordering bug for a brief
    // moment on November 20, 2286
    std::sort(mut_filenames.begin(), mut_filenames.end());

    // Iterate over the names of all files, starting with the oldest
    size_t num_uploads_attempted = 0;
    size_t num_uploads_completed_successfully = 0;
    _process_and_upload_batch_result last_batch_result{
        _process_and_upload_batch_result::success
    };
    for (const std::string& filename : mut_filenames)
    {
        // If a filename is all-integer, it's taken to be a Unix timestamp in
        // milliseconds, corresponding to the system_clock value that the storage thread
        // read when it created the file
        uint64_t timestamp_ms{0};
        const auto parse_result = std::from_chars(
            filename.data(), filename.data() + filename.size(), timestamp_ms
        );

        // Skip any files whose names are not strictly numeric
        const bool parse_ok = parse_result.ec == std::errc{};
        const bool parsed_full_string =
            parse_result.ptr == (filename.data() + filename.size());
        if (!parse_ok || !parsed_full_string)
        {
            continue;
        }

        // Read the system clock so we can determine the relative age of the file
        const platform::Timestamp file_time{std::chrono::milliseconds(timestamp_ms)};
        const platform::Timestamp now = clock.Now();

        // Defensive: guard against integer underflow on file age calculation. If the
        // file appears to be from the future, clamp its age to 0.
        platform::Duration file_age = platform::Duration::zero();
        if (file_time < now)
        {
            file_age = now - file_time;
        }

        // Under normal circumstances, we have to respect a minimum file age threshold
        // before we can read from files, as the storage thread may still be writing to
        // them before they hit that age. If our threshold is 0, it means we're
        // permitted to bypass these checks and read from all files, as long as they're
        // not too _old_ to upload.
        if (file_age < config.min_file_age_for_read)
        {
            // If we've encountered our first file that's too new to process, we're
            // done. We process files in order, sorted lexically by timestamp, so every
            // file that we'd encounter hereafter would be even newer than this one.
            break;
        }

        // If this file is too old to process, delete it and continue
        if (file_age >= config.max_file_age_for_read)
        {
            if (!directory.DeleteFile(filename))
            {
                // TODO: Keep track of the files we've processed, so that even if we
                // fail to delete a batch, we won't continually reupload it
                std::cout << "<UPLOAD> " << feature.name << " (batch" << filename
                          << "): ERROR: delete failed\n";
            }
            continue;
        }

        // Read the batch of event data from the file, pass it to the feature
        // implementation to be processed, then initiate the resulting HTTP request to
        // upload the batch to intake
        last_batch_result = _process_and_upload_batch(
            core_context,
            *feature.impl,
            directory,
            http_client,
            filename,
            mut_read_buffer
        );
        num_uploads_attempted++;

        // Decide what to do with this file, and whether we should keep going
        bool should_delete_batch = false;
        bool should_abort_upload_cycle = false;
        switch (last_batch_result)
        {
            case _process_and_upload_batch_result::success:
                std::cout << "<UPLOAD> " << feature.name << " (batch" << filename
                          << "): Upload OK\n";
                num_uploads_completed_successfully++;
                should_delete_batch = true;
                break;
            case _process_and_upload_batch_result::retryable_failure:
                std::cout << "<UPLOAD> " << feature.name << " (batch" << filename
                          << "): Failed; will retry\n";
                should_abort_upload_cycle = true;
                break;
            case _process_and_upload_batch_result::bad_batch:
                std::cout << "<UPLOAD> " << feature.name << " (batch" << filename
                          << "): Bad batch; will delete\n";
                should_delete_batch = true;
                break;
        }

        // If we need to delete the batch file, either because we processed it
        // successfully or because it's somehow malformed, delete it
        if (should_delete_batch)
        {
            if (!directory.DeleteFile(filename))
            {
                // TODO: Keep track of the files we've processed, so that even if we
                // fail to delete a batch, we won't continually reupload it
                std::cout << "<UPLOAD> " << feature.name << " (batch" << filename
                          << "): ERROR: delete failed\n";
            }
            std::cout << "<UPLOAD> " << feature.name << " (batch" << filename
                      << "): deleted\n";
        }

        // Break out of the loop if we don't want to continue processing batches
        if (should_abort_upload_cycle)
        {
            break;
        }

        // If we would otherwise keep going but we've hit our limit on the number of
        // batches processed in one upload cycle, stop processing
        if (num_uploads_attempted >= config.max_batches_per_cycle)
        {
            break;
        }
    }

    std::cout << "<UPLOAD> " << feature.name << ": cycle finished with "
              << num_uploads_attempted << " uploads attempted; "
              << num_uploads_completed_successfully << " uploads successful\n";

    // If we didn't find any batches to upload, leave our backoff interval unchanged
    if (num_uploads_attempted == 0)
    {
        // Schedule the next upload cycle after the same delay
        return feature.upload_state->current_delay;
    }

    // We did try to process one or more batches: modify this feature's backoff interval
    // based on the result of the last batch
    if (last_batch_result == _process_and_upload_batch_result::success)
    {
        // Last batch was good; schedule the next upload cycle to happen sooner
        return feature.upload_state->ResetDelayToMin();
    }

    // Last batch failed; wait a bit longer for the next upload cycle
    return feature.upload_state->IncreaseDelayTowardMax();
}

UploadThreadConfig UploadThreadConfig::FromCoreConfig(
    BatchSize batch_size,
    BatchProcessingLevel batch_processing_level
)
{
    return UploadThreadConfig(
        BatchSize_ToMinFileAgeForRead(batch_size),
        BatchProcessingLevel_ToMaxBatchesPerCycle(batch_processing_level)
    );
}

platform::Duration Internal_HandleUploadProc(
    UploadThreadConfig config,
    const CoreContext& core_context,
    const platform::IClock& clock,
    FeatureId feature_id,
    std::vector<RegisteredFeature>& features,
    platform::IHttpClient& http_client,
    std::vector<std::string>& mut_filenames,
    std::vector<char>& mut_read_buffer
)
{
    // Find the feature that we want to initiate an upload cycle for
    const auto feature = std::find_if(
        features.begin(),
        features.end(),
        [feature_id](const RegisteredFeature& f)
        {
            return f.id == feature_id;
        }
    );

    // If we don't have a matching feature, something is wrong, since the set of
    // registered features can not be modified during the lifetime of the upload thread,
    // and the upload thread should be the only thing scheduling uploads
    if (feature == features.end())
    {
        assert(false && "feature_id on scheduled upload matches no registered feature");
        return ASSERTION_FAILURE_BACKOFF;
    }

    // Kick off the next upload cycle for this feature, scanning the appropriate storage
    // directory for files that need to be uploaded, and uploading any that are found,
    // up to the configured limits
    return _run_upload_cycle(
        config,
        core_context,
        clock,
        *feature,
        http_client,
        mut_filenames,
        mut_read_buffer
    );
}

void UploadThreadMain(
    UploadThreadConfig config,
    const CoreContext& core_context,
    const platform::IClock& clock,
    UploadScheduler& scheduler,
    std::vector<RegisteredFeature>& features,
    platform::IHttpClient& http_client
)
{
    std::cout << "<UPLOAD> Started\n";

    // Schedule initial upload cycles for all features
    for (auto& feature : features)
    {
        std::cout << "<UPLOAD> First cycle in "
                  << feature.upload_state->current_delay.count() << "\n";
        scheduler.Schedule(feature.id, feature.upload_state->current_delay);
    }

    // Initialize a reusable vector to contain filenames, so we don't have to allocate
    // every time we scan through a directory
    std::vector<std::string> filenames;
    filenames.reserve(64);

    // Similarly, initialize a reusable buffer to contain the current TLV block data
    // read from each file: we process uploads serially, so this buffer can be reused
    // between uploads and between features
    std::vector<char> read_buffer;
    read_buffer.reserve(QuantizeBufferSize(1024));

    // Run indefinitely, exiting once the scheduler returns nullopt
    while (auto feature_id = scheduler.WaitForNext())
    {
        const platform::Duration delay_until_next_cycle = Internal_HandleUploadProc(
            config,
            core_context,
            clock,
            *feature_id,
            features,
            http_client,
            filenames,
            read_buffer
        );
        scheduler.Schedule(*feature_id, delay_until_next_cycle);
    }

    std::cout << "<UPLOAD> Finished\n";
}

} // namespace datadog::impl
