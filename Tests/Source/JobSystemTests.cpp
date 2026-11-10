#include "Core/JobSystem.h"

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <numeric>
#include <set>
#include <thread>
#include <vector>

using namespace arc;

TEST_CASE("A job system starts the requested number of workers")
{
    JobSystem jobs(2);
    CHECK(jobs.workerCount() == 2);
    JobSystem automatic;
    CHECK(automatic.workerCount() >= 1);
}

TEST_CASE("wait() returns once every job of the group has run")
{
    JobSystem jobs(3);
    std::atomic<uint32_t> sum{0};
    JobCounter counter;
    for (uint32_t i = 1; i <= 100; ++i)
        jobs.run(counter, [&sum, i] { sum.fetch_add(i, std::memory_order_relaxed); });
    jobs.wait(counter);
    CHECK(counter.done());
    CHECK(sum.load() == 5050);
    CHECK(jobs.jobsRun() >= 100);
}

TEST_CASE("Waiting with no jobs returns at once")
{
    JobSystem jobs(1);
    JobCounter counter;
    jobs.wait(counter);
    CHECK(counter.done());
}

TEST_CASE("parallelFor covers every index exactly once, in batches no larger than asked")
{
    JobSystem jobs(3);
    for (const uint32_t count : {0u, 1u, 7u, 64u, 1000u, 4097u})
        for (const uint32_t batch : {1u, 3u, 64u, 5000u})
        {
            std::vector<std::atomic<uint32_t>> hits(count);
            std::atomic<uint32_t> largestBatch{0};
            jobs.parallelFor(count, batch, [&](uint32_t begin, uint32_t end) {
                CHECK(begin < end);
                uint32_t size = end - begin;
                uint32_t seen = largestBatch.load();
                while (size > seen && !largestBatch.compare_exchange_weak(seen, size))
                {
                }
                for (uint32_t i = begin; i < end; ++i)
                    hits[i].fetch_add(1, std::memory_order_relaxed);
            });
            bool allOnce = true;
            for (const auto& hit : hits)
                allOnce = allOnce && hit.load() == 1;
            CHECK(allOnce);
            CHECK(largestBatch.load() <= batch);
        }
}

TEST_CASE("parallelFor runs on more than one thread")
{
    JobSystem jobs(3);
    std::mutex mutex;
    std::set<std::thread::id> threads;
    jobs.parallelFor(64, 1, [&](uint32_t, uint32_t) {
        // Long enough that the workers pick up batches before the calling thread finishes them all.
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        const std::scoped_lock lock(mutex);
        threads.insert(std::this_thread::get_id());
    });
    CHECK(threads.size() > 1);
}

TEST_CASE("Jobs may wait on jobs they start without deadlocking the pool")
{
    // More nested waits than workers: each job's wait runs queued jobs itself.
    JobSystem jobs(2);
    std::atomic<uint32_t> leaves{0};
    JobCounter outer;
    for (int i = 0; i < 8; ++i)
        jobs.run(outer, [&] {
            JobCounter inner;
            for (int j = 0; j < 8; ++j)
                jobs.run(inner, [&] { leaves.fetch_add(1, std::memory_order_relaxed); });
            jobs.wait(inner);
        });
    jobs.wait(outer);
    CHECK(leaves.load() == 64);

    // The same through nested parallelFor.
    std::atomic<uint64_t> total{0};
    jobs.parallelFor(16, 1, [&](uint32_t, uint32_t) {
        jobs.parallelFor(100, 7, [&](uint32_t begin, uint32_t end) {
            for (uint32_t i = begin; i < end; ++i)
                total.fetch_add(i, std::memory_order_relaxed);
        });
    });
    CHECK(total.load() == 16u * 4950u);
}

TEST_CASE("Jobs still queued when the job system is destroyed run first")
{
    std::atomic<uint32_t> ran{0};
    JobCounter counter;
    {
        JobSystem jobs(1);
        for (int i = 0; i < 50; ++i)
            jobs.run(counter, [&] { ran.fetch_add(1, std::memory_order_relaxed); });
    }
    CHECK(ran.load() == 50);
    CHECK(counter.done());
}

TEST_CASE("The free parallelFor runs inline without an active job system")
{
    JobSystem* previous = activeJobSystem();
    setActiveJobSystem(nullptr);
    std::vector<uint32_t> order;
    parallelFor(10, 4, [&](uint32_t begin, uint32_t end) {
        for (uint32_t i = begin; i < end; ++i)
            order.push_back(i);
    });
    setActiveJobSystem(previous);
    std::vector<uint32_t> expected(10);
    std::iota(expected.begin(), expected.end(), 0u);
    CHECK(order == expected);
}
