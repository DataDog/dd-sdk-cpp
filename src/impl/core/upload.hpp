#pragma once

#include <cassert>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <vector>

#include "core/feature.hpp"
#include "core/types.hpp"
#include "platform/clock.hpp"
#include "platform/http.hpp"

namespace datadog::impl {

constexpr platform::Duration from_seconds(double sec)
{
    return std::chrono::duration_cast<platform::Duration>(
        std::chrono::duration<double>(sec)
    );
}

/**
 * Global configuration details for the upload thread, to tune its behavior re: file age
 * cutoffs.
 */
struct UploadThreadConfig
{
    /**
     * Until a file reaches this age, the upload thread will consider it still owned by
     * the storage thread and will refrain from reading or deleting it.
     */
    platform::Duration min_file_age_for_read;

    /**
     * Once a file reacheds this age, the upload thread will refrain from processing it
     * for upload, and will instead delete it outright.
     *
     * TODO: This is meant to be configurable per-feature (so that RUM events can
     * persist on disk for 24h, rather than the default 18h); move it elsehwere.
     */
    platform::Duration max_file_age_for_read{std::chrono::hours(18)};

    /**
     * Inclusive upper limit on the number of batch files that may be processed and
     * uploaded in a single upload cycle for a single feature.
     */
    size_t max_batches_per_cycle;

    explicit UploadThreadConfig(
        platform::Duration in_min_file_age_for_read,
        size_t in_max_batches_per_cycle
    )
        : min_file_age_for_read(in_min_file_age_for_read)
        , max_batches_per_cycle(in_max_batches_per_cycle)
    {
    }

    static UploadThreadConfig
    FromCoreConfig(BatchSize batch_size, BatchProcessingLevel batch_processing_level);
};

/**
 * Feature-specific state used by the upload thread to control the timing of upload
 * cycles.
 *
 * Implements adaptive backoff on a per-feature basis: if HTTP requests for a particular
 * feature's uploads fail continually, the time between upload attempts for that feature
 * will increase as a result.
 */
struct UploadThreadState
{
    platform::Duration current_delay;
    platform::Duration min_delay;
    platform::Duration max_delay;

    explicit UploadThreadState(UploadFrequency upload_frequency)
        : current_delay{}
        , min_delay{}
        , max_delay{}
    {
        // Initialize best-case interval between requests, when network conditions are
        // good and uploads are succeeding
        switch (upload_frequency)
        {
            case UploadFrequency::Frequent:
                min_delay = from_seconds(3.0);
                break;
            case UploadFrequency::Average:
                min_delay = from_seconds(10.0);
                break;
            case UploadFrequency::Rare:
                min_delay = from_seconds(35.0);
                break;
        }

        // Clamp worst-case interval at 10x the best-case delay, and begin with an
        // initial starting point that's halfway between the two
        current_delay = min_delay * 5;
        max_delay = min_delay * 10;
    }

    platform::Duration IncreaseDelayTowardMax()
    {
        const int64_t current = current_delay.count();
        const int64_t ten_percent =
            static_cast<int64_t>(static_cast<double>(current) * 0.1);
        current_delay =
            platform::Duration(std::min(max_delay.count(), current + ten_percent));
        return current_delay;
    }

    platform::Duration ResetDelayToMin()
    {
        current_delay = min_delay;
        return current_delay;
    }
};

/**
 * Keeps track of when upload cycles should run next for all registered features.
 */
class UploadScheduler
{
    struct Item
    {
        FeatureId feature_id;
        platform::Timestamp next_cycle_at;

        bool operator>(const Item& other) const
        {
            return next_cycle_at > other.next_cycle_at;
        }
    };

    /**
     * Used to read the current system time.
     */
    const platform::IClock& _clock;
    /**
     * Flag used to signal that scheduling is stopped and the upload thread should exit.
     */
    std::atomic<bool> _stopped{false};
    /**
     * Min-heap containing the timestamps at which the next upload cycle for each
     * feature should begin. Only accessed from the upload thread.
     */
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> _pq;
    /**
     * Wakes the upload thread in response to shutdown, so that the upload thread can
     * sleep for long periods of time without periodically waking up to check the
     * shutdown flag.
     */
    std::condition_variable _cv;
    /**
     * Synchronizes access to _cv. Does NOT synchronize access to _pq, as _pq is only
     * accessed on the upload thread.
     */
    std::mutex _mutex;

public:
    explicit UploadScheduler(const platform::IClock& clock)
        : _clock(clock)
    {
    }

    void Stop()
    {
        // Set the atomic shutdown flag so that if any thread is blocked in SleepUntil,
        // it will see on the next wakeup that it's time to exit
        _stopped.store(true, std::memory_order_release);

        // Signal the condition_variable to trigger a wakeup of any waiting threads
        {
            std::lock_guard lock(_mutex);
            _cv.notify_all();
        }
    }

    void Schedule(FeatureId feature_id, platform::Duration next_cycle_in)
    {
        // Add an entry to the priority queue, maintaining earliest-first ordering.
        // NOTE: Calls to Schedule() and WaitForNext() do not happen concurrently: the
        // upload thread uses this queue in a single-threaded fashion. Therefore, we
        // don't need to synchronize those operations or worry about waking consumers in
        // response to this push call.
        platform::Timestamp next_cycle_at = _clock.Now() + next_cycle_in;
        _pq.push({feature_id, next_cycle_at});
    }

    std::optional<FeatureId> WaitForNext()
    {
        // We shouldn't ordinarily encounter an empty queue, as usage of the priority
        // queue is single-threaded within the upload thread, and we always replace an
        // item after processing it
        if (_pq.empty())
        {
            return std::nullopt;
        }

        // Peek the top-priority element, i.e. the scheduled upload with the lowest
        // next_cycle_at timestamp
        const auto& item = _pq.top();

        // If the next feature isn't ready yet, sleep until it is
        const platform::Timestamp now = _clock.Now();
        if (item.next_cycle_at > now)
        {
            // If we're awoken because of a shutdown, return nullopt to halt further
            // processing and signal that the upload thread should exit
            const platform::Duration sleep_duration = item.next_cycle_at - now;
            const bool is_shutting_down = SleepFor(sleep_duration);
            if (is_shutting_down)
            {
                return std::nullopt;
            }
        }

        // It's time to start a new upload cycle for this feature: return its ID to the
        // caller, popping it off the queue to be replaced by the caller.
        //
        // NOTE: Once again, all calls to WaitForNext() and Schedule() happen in the
        // same thread, so we don't need synchronization here: it's guaranteed that _pq
        // is in the same state here as it was before the SleepUntil() call.
        FeatureId feature_id = item.feature_id;
        _pq.pop();
        return feature_id;
    }

private:
    bool SleepFor(platform::Duration duration)
    {
        // Wait for the desired delay, -or- until the atomic shutdown flag is set
        std::unique_lock lock(_mutex);
        return _cv.wait_for(
            lock,
            duration,
            [&]
            {
                // On spurious wakeup, go back to sleep unless shutdown flag is set
                return _stopped.load(std::memory_order_acquire);
            }
        );
    }
};

/**
 * Initiates an upload cycle for the given feature.
 *
 * Called by UploadThreadMain when a feature is ready to be processed by the upload
 * thread. Exposed here to facilitate unit testing.
 */
platform::Duration Internal_HandleUploadProc(
    UploadThreadConfig config,
    const CoreContext& core_context,
    const platform::IClock& clock,
    FeatureId feature_id,
    std::vector<struct RegisteredFeature>& features,
    platform::IHttpClient& http_client,
    std::vector<std::string>& mut_filenames,
    std::vector<char>& mut_read_buffer
);

/**
 * Entry point for the upload thread. See description in `core.hpp`.
 *
 * @param config Global configuration values for the upload thread.
 * @param core_context The context values held by the SDK when the thread started;
 *  passed to UploadThread_PrepareReport to inform request headers etc. TODO: changes
 *  made via SetService() and SetEnv() do not propagate to this thread.
 * @param clock Interface to the system clock.
 * @param scheduler Non-owning reference to the object used to coordinate the timing of
 *  upload cycles for each registered feature. The main thread owns the scheduler and
 *  uses it to signal shutdown; but actual scheduling state is exclusive to the upload
 *  thread.
 * @param features Non-owning reference to the array of registered features. The vector
 *  and its items are guaranteed to remain immutable and to persist for the lifetime of
 *  the thread.
 * @param http_client Non-owning reference to the HTTP client that will be used to
 *  initiate requests for each report.
 */
void UploadThreadMain(
    UploadThreadConfig config,
    const CoreContext& core_context,
    const platform::IClock& clock,
    UploadScheduler& scheduler,
    std::vector<struct RegisteredFeature>& features,
    platform::IHttpClient& http_client
);

} // namespace datadog::impl
