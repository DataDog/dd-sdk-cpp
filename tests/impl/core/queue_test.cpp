#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <thread>
#include <vector>

#include "core/queue.hpp"

using namespace datadog::impl;

TEST_CASE("Queue<int>", "[unit]")
{
    SECTION("M allow push and pop W queue is active")
    {
        // Given a queue with one item
        Queue<int> queue;
        REQUIRE(queue.Push(42) == true);

        // When Pop() is called
        auto result = queue.Pop();

        // Then that item is returned
        REQUIRE(result.has_value());
        REQUIRE(result.value() == 42);

        // Manual cleanup for unit test (in ordinary usage, shutdown must be
        // synchronized with externally-managed threads are waiting on queue, so calling
        // Stop() via RAII is not supported)
        queue.Stop();
    }

    SECTION("M transfer ownership W item is pushed and popped")
    {
        // Given a queue with a move-only item item
        Queue<std::unique_ptr<int>> queue;

        // When an rvalue is passed to Push()
        auto item = std::make_unique<int>(42);
        int* raw_ptr = item.get();
        REQUIRE(queue.Push(std::move(item)) == true);

        // Then ownership is transferred to the queue
        REQUIRE(item == nullptr);

        // And: When the item is returned from Pop()
        auto result = queue.Pop();

        // Then ownership is transferred to the caller
        REQUIRE(result.has_value());
        REQUIRE(result.value().get() == raw_ptr);
        REQUIRE(*(result.value()) == 42);

        // Manual cleanup
        queue.Stop();
    }

    SECTION("M maintain FIFO order W multiple items pushed")
    {
        // Given a queue with three items 1, 2, 3
        Queue<int> queue;
        REQUIRE(queue.Push(1) == true);
        REQUIRE(queue.Push(2) == true);
        REQUIRE(queue.Push(3) == true);

        // When those three items are popped
        auto first = queue.Pop();
        auto second = queue.Pop();
        auto third = queue.Pop();

        // Then the caller receives them in the same order 1, 2, 3
        REQUIRE(first.value() == 1);
        REQUIRE(second.value() == 2);
        REQUIRE(third.value() == 3);

        // Manual cleanup
        queue.Stop();
    }

    SECTION("M reject push W queue is stopped")
    {
        // Given a queue
        Queue<int> queue;

        // When the queue is stopped
        queue.Stop();

        // Then pushing new items will fail
        REQUIRE(queue.Push(42) == false);
    }

    SECTION("M return nullopt W queue is stopped and empty")
    {
        // Given a queue
        Queue<int> queue;

        // When the queue is stopped
        queue.Stop();

        // Then Pop() will return nullopt without blocking
        auto result = queue.Pop();
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("M drain existing items W queue is stopped")
    {
        // Given a queue that contains two items
        Queue<int> queue;
        REQUIRE(queue.Push(1) == true);
        REQUIRE(queue.Push(2) == true);

        // When the queue is stopped
        queue.Stop();

        // Then items should still be returned from Pop()
        auto first = queue.Pop();
        auto second = queue.Pop();
        REQUIRE(first.value() == 1);
        REQUIRE(second.value() == 2);

        // And the queue should be fully drained thereafter
        auto third = queue.Pop();
        REQUIRE_FALSE(third.has_value());
    }

    SECTION("M reject new items after stop W queue has existing items")
    {
        // Given a queue with one item
        Queue<int> queue;
        REQUIRE(queue.Push(1) == true);

        // When the queue is stopped
        queue.Stop();

        // Then subsequent items should be dropped
        REQUIRE(queue.Push(2) == false);

        // And new items should still be returned from pop
        auto result = queue.Pop();
        REQUIRE(result.value() == 1);
    }
}

TEST_CASE("Queue threading", "[unit]")
{
    SECTION("M handle concurrent pushes W multiple producer threads")
    {
        // Given a queue
        Queue<int> queue;

        // And multiple producer threads that all push lots of items into the queue
        const int num_threads = 4;
        const int items_per_thread = 100;
        std::vector<std::thread> producers;
        for (int t = 0; t < num_threads; ++t)
        {
            producers.emplace_back(
                [&queue, t, items_per_thread]()
                {
                    for (int i = 0; i < items_per_thread; ++i)
                    {
                        int value = t * items_per_thread + i;
                        queue.Push(std::move(value));
                    }
                }
            );
        }

        // And a single consumer thread that pops items and adds them to this vector
        std::vector<int> consumed;
        std::thread consumer(
            [&queue, &consumed, num_threads, items_per_thread]()
            {
                for (int expected = 0; expected < num_threads * items_per_thread;
                     ++expected)
                {
                    auto item = queue.Pop();
                    if (item.has_value())
                    {
                        consumed.push_back(item.value());
                    }
                    else
                    {
                        break;
                    }
                }
            }
        );

        // When all producers have finished
        for (auto& producer : producers)
        {
            producer.join();
        }

        // And the queue is stopped
        queue.Stop();

        // And all consumers have finished
        consumer.join();

        // Then the vector contains all values that were produced
        REQUIRE(consumed.size() == num_threads * items_per_thread);
        std::sort(consumed.begin(), consumed.end());
        for (size_t i = 0; i < consumed.size(); ++i)
        {
            REQUIRE(consumed[i] == static_cast<int>(i));
        }
    }

    SECTION("M block pop until item available W queue initially empty")
    {
        // Given a queue
        Queue<int> queue;

        // And a consumer thread that will block on Pop()
        std::atomic<bool> popped{ false };
        std::atomic<int> result{ 0 };
        std::thread consumer(
            [&queue, &popped, &result]()
            {
                auto item = queue.Pop();
                if (item.has_value())
                {
                    result.store(item.value());
                    popped.store(true);
                }
            }
        );

        // And a brief delay to allow the consumer thread to start and block
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        // Then the consumer is still waiting for an item
        REQUIRE_FALSE(popped.load());

        // Given this setup,
        // When we push a value into the queue
        REQUIRE(queue.Push(42) == true);

        // Then the consumer thread receives value and exits
        consumer.join();
        REQUIRE(popped.load());
        REQUIRE(result.load() == 42);

        // Manual cleanup
        queue.Stop();
    }

    SECTION("M wake all waiting consumers W stop is called")
    {
        // Given a queue
        Queue<int> queue;

        // And three consumers that will increment finished_consumers on exit
        const int num_consumers = 3;
        std::atomic<int> finished_consumers{ 0 };
        std::vector<std::thread> consumers;
        for (int i = 0; i < num_consumers; ++i)
        {
            consumers.emplace_back(
                [&queue, &finished_consumers]()
                {
                    while (true)
                    {
                        auto item = queue.Pop();
                        if (!item.has_value())
                        {
                            finished_consumers.fetch_add(1);
                            break;
                        }
                    }
                }
            );
        }

        // And a brief delay to ensure consumers are blocking
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        // When the queue is stopped
        queue.Stop();

        // Then all consumers will exit
        for (auto& consumer : consumers)
        {
            consumer.join();
        }
        REQUIRE(finished_consumers.load() == num_consumers);
    }
}
