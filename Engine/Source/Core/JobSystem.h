#pragma once

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace arc
{
    /// Counts the jobs of a group that have not finished. JobSystem::run() adds one, the job's end
    /// takes it away, and JobSystem::wait() returns at zero. Must outlive the jobs it counts.
    class JobCounter
    {
    public:
        JobCounter() = default;
        JobCounter(const JobCounter&) = delete;
        JobCounter& operator=(const JobCounter&) = delete;

        [[nodiscard]] bool done() const { return m_pending.load(std::memory_order_acquire) == 0; }

    private:
        friend class JobSystem;
        std::atomic<uint32_t> m_pending{0};
    };

    /// A pool of worker threads that run jobs from one shared queue.
    ///
    /// A job is any callable. Jobs are grouped by a JobCounter, and the thread that waits on one
    /// runs queued jobs itself until the group is done, so waiting never leaves a core idle and a job
    /// may wait on jobs it started (nested parallelism) without deadlocking the pool.
    ///
    /// Long jobs that no frame waits for, such as loading an asset, go to runBackground() instead:
    /// only workers run them, never a thread that waits, and they never take every worker.
    ///
    /// Jobs must not throw, and must not touch what other jobs of the same group write without
    /// their own synchronisation. The Application owns one, with a worker per hardware thread but the
    /// main thread, and makes it the active one (see activeJobSystem()).
    class JobSystem
    {
    public:
        /// 0 workers means one per hardware thread, less one for the thread that waits. There is
        /// always at least one. `onWorkerExit` runs on each worker as it exits, to free what
        /// libraries keep per thread for threads they did not create (SDL's error message buffer).
        explicit JobSystem(uint32_t workerCount = 0, std::function<void()> onWorkerExit = {});

        /// Waits for the queued jobs to run, then joins the workers.
        ~JobSystem();

        JobSystem(const JobSystem&) = delete;
        JobSystem& operator=(const JobSystem&) = delete;

        [[nodiscard]] uint32_t workerCount() const { return static_cast<uint32_t>(m_workers.size()); }

        /// Queues `job` to run on a worker (or on a thread waiting in wait()).
        void run(JobCounter& counter, std::function<void()> job);

        /// Queues `job` to run on a worker once fewer than backgroundLimit() background jobs are
        /// running. wait() never runs it on the waiting thread, so a frame that waits on its own jobs
        /// is not held up by a background job it happened to pick up. Foreground jobs go first.
        void runBackground(JobCounter& counter, std::function<void()> job);

        /// How many background jobs may run at once: every worker but one, which stays free for the
        /// frame's work, or 1 with a single worker.
        [[nodiscard]] uint32_t backgroundLimit() const { return m_backgroundLimit; }

        /// Blocks until every job counted by `counter` has finished, running queued foreground jobs on
        /// the calling thread meanwhile. Those may belong to other groups.
        void wait(JobCounter& counter);

        /// Calls `body(begin, end)` for consecutive ranges of at most `batchSize` indices that together
        /// cover [0, count), on the workers and the calling thread, and returns once all have run.
        /// Ranges are handed out one at a time as threads become free, so uneven batches balance out.
        template <typename Body>
        void parallelFor(uint32_t count, uint32_t batchSize, const Body& body);

        /// Jobs run since the pool was created, by workers and waiting threads together.
        [[nodiscard]] uint64_t jobsRun() const { return m_jobsRun.load(std::memory_order_relaxed); }

    private:
        struct Job
        {
            std::function<void()> function;
            JobCounter* counter = nullptr;
        };

        void workerLoop(uint32_t index);
        [[nodiscard]] bool tryPop(Job& job);
        [[nodiscard]] bool canStartBackground() const;
        void execute(Job& job, bool background);

        std::vector<std::thread> m_workers;
        std::deque<Job> m_queue;
        std::deque<Job> m_background;
        uint32_t m_backgroundRunning = 0;
        uint32_t m_backgroundLimit = 1;
        std::mutex m_mutex;
        // Workers sleep on m_jobAvailable while there is nothing they may start; waiting threads sleep on
        // m_jobFinished while their group still has jobs running on other threads.
        std::condition_variable m_jobAvailable;
        std::condition_variable m_jobFinished;
        bool m_stopping = false;
        std::atomic<uint64_t> m_jobsRun{0};
        std::function<void()> m_onWorkerExit;
    };

    /// The job system engine code runs parallel work on, or nullptr when there is none (unit tests
    /// that did not install one, tools), in which case the free parallelFor() below runs inline.
    [[nodiscard]] JobSystem* activeJobSystem();
    void setActiveJobSystem(JobSystem* jobs);

    /// JobSystem::parallelFor() on the active job system, or a plain loop over the batches without one.
    template <typename Body>
    void parallelFor(uint32_t count, uint32_t batchSize, const Body& body)
    {
        if (JobSystem* jobs = activeJobSystem())
        {
            jobs->parallelFor(count, batchSize, body);
            return;
        }
        batchSize = std::max(batchSize, 1u);
        for (uint32_t begin = 0; begin < count; begin += batchSize)
            body(begin, std::min(count, begin + batchSize));
    }

    template <typename Body>
    void JobSystem::parallelFor(uint32_t count, uint32_t batchSize, const Body& body)
    {
        batchSize = std::max(batchSize, 1u);
        const uint32_t batches = (count + batchSize - 1) / batchSize;
        if (batches == 0)
            return;

        // A few helper jobs that each take the next batch until none are left, rather than a job per
        // batch: less queue traffic, and a slow batch does not hold up a fixed share of the work.
        std::atomic<uint32_t> next{0};
        const auto drain = [&] {
            for (uint32_t batch = next.fetch_add(1, std::memory_order_relaxed); batch < batches;
                 batch = next.fetch_add(1, std::memory_order_relaxed))
            {
                const uint32_t begin = batch * batchSize;
                body(begin, std::min(count, begin + batchSize));
            }
        };

        JobCounter counter;
        const uint32_t helpers = std::min(batches - 1, workerCount());
        for (uint32_t i = 0; i < helpers; ++i)
            run(counter, drain);
        drain();
        wait(counter);
    }
}
