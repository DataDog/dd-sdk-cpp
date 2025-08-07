#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <vector>

#include "core/feature.hpp"
#include "core/types.hpp"
#include "platform/http.hpp"

namespace datadog::impl {

using system_clock = std::chrono::system_clock;
using duration = std::chrono::duration<uint64_t, std::nano>;
using time_point = std::chrono::time_point<system_clock, duration>;

constexpr duration from_seconds(double sec)
{
    return std::chrono::duration_cast<duration>(std::chrono::duration<double>(sec));
}

struct UploadThreadState
{
    duration current_delay;
    duration min_delay;
    duration max_delay;

    UploadThreadState(UploadFrequency upload_frequency)
        : current_delay(0)
        , min_delay(0)
        , max_delay(0)
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

    duration IncreaseDelayTowardMax()
    {
        const uint64_t current = current_delay.count();
        const uint64_t ten_percent =
            static_cast<uint64_t>(static_cast<double>(current) * 0.1);
        current_delay = duration(std::min(max_delay.count(), current + ten_percent));
        return current_delay;
    }

    duration ResetDelayToMin()
    {
        current_delay = min_delay;
        return current_delay;
    }
};

class UploadScheduler
{
    struct Item
    {
        FeatureId feature_id;
        time_point next_cycle_at;

        bool operator>(const Item& other) const
        {
            return next_cycle_at > other.next_cycle_at;
        }
    };

    /**
     * Flag used to signal that the scheduling is stopped and the upload thread should
     * exit.
     */
    std::atomic<bool> _stopped{ false };
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

    void Schedule(FeatureId feature_id, time_point next_cycle_at)
    {
        // Add an entry to the priority queue, maintaining earliest-first ordering.
        // NOTE: Calls to Schedule() and WaitForNext() do not happen concurrently: the
        // upload thread uses this queue in a single-threaded fashion. Therefore, we
        // don't need to synchronize those operations or worry about waking consumers in
        // response to this push call.
        _pq.push({ feature_id, next_cycle_at });
    }

    std::optional<FeatureId> WaitForNext(time_point current_time)
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
        if (item.next_cycle_at > current_time)
        {
            // If we're awoken because of a shutdown, return nullopt to halt further
            // processing and signal that the upload thread should exit
            const bool is_shutting_down = SleepUntil(item.next_cycle_at);
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
    bool SleepUntil(time_point time)
    {
        // Wait until the desired time, -or- until the atomic shutdown flag is set
        std::unique_lock lock(_mutex);
        return _cv.wait_until(
            lock,
            time,
            [&]
            {
                // On spurious wakeup, go back to sleep unless shutdown flag is set
                return _stopped.load(std::memory_order_acquire);
            }
        );
    }
};

/**
 * Entry point for the upload thread. See description in `core.hpp`.
 *
 * @param core_context The context values held by the SDK when the thread started;
 *  passed to UploadThread_PrepareReport to inform request headers etc. TODO: changes
 *  made via SetService() and SetEnv() do not propagate to this thread.
 * @param scheduler Non-owning reference to the object used to coordinate the timing of
 *  upload cycles for each registered feature. The main thread owns the scheduler and
 *  uses it to signal shutdown; but actual scheduling state is exclusive to the upload
 *  thread.
 * @param features Non-owning reference to the array of registered features. The vector
 *  and its items are guaranteed to remain immutable and persist for the lifetime of the
 *  thread.
 * @param http_client Non-owning reference to the HTTP client that will be used to
 *  initiate requests for each report.
 */
void UploadThreadMain(
    const CoreContext& core_context,
    UploadScheduler& scheduler,
    std::vector<struct RegisteredFeature>& features,
    platform::IHttpClient& http_client
);

}
