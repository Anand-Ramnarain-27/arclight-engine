#include "Core/JobSystem.h"

#include "Core/Assert.h"
#include "Core/Profiler.h"

#include <format>
#include <string>

namespace arc
{
    namespace
    {
        std::atomic<JobSystem*> g_activeJobSystem{nullptr};
    }

    JobSystem* activeJobSystem()
    {
        return g_activeJobSystem.load(std::memory_order_acquire);
    }

    void setActiveJobSystem(JobSystem* jobs)
    {
        g_activeJobSystem.store(jobs, std::memory_order_release);
    }

    JobSystem::JobSystem(uint32_t workerCount)
    {
        if (workerCount == 0)
        {
            // hardware_concurrency() may report 0 when it cannot tell.
            const uint32_t threads = std::max(std::thread::hardware_concurrency(), 2u);
            workerCount = threads - 1;
        }
        m_workers.reserve(workerCount);
        for (uint32_t i = 0; i < workerCount; ++i)
            m_workers.emplace_back([this, i] { workerLoop(i); });
    }

    JobSystem::~JobSystem()
    {
        {
            const std::scoped_lock lock(m_mutex);
            m_stopping = true;
        }
        m_jobAvailable.notify_all();
        for (std::thread& worker : m_workers)
            worker.join();
        if (activeJobSystem() == this)
            setActiveJobSystem(nullptr);
    }

    void JobSystem::run(JobCounter& counter, std::function<void()> job)
    {
        counter.m_pending.fetch_add(1, std::memory_order_relaxed);
        {
            const std::scoped_lock lock(m_mutex);
            ARC_ASSERT(!m_stopping, "Job queued while the job system shuts down");
            m_queue.push_back({std::move(job), &counter});
        }
        m_jobAvailable.notify_one();
        // A thread in wait() may be asleep with nothing to run; it can take this one.
        m_jobFinished.notify_all();
    }

    bool JobSystem::tryPop(Job& job)
    {
        const std::scoped_lock lock(m_mutex);
        if (m_queue.empty())
            return false;
        job = std::move(m_queue.front());
        m_queue.pop_front();
        return true;
    }

    void JobSystem::execute(Job& job)
    {
        {
            ARC_PROFILE_SCOPE("Job");
            job.function();
        }
        // Released before the counter drops, so a waiter that sees zero also sees what the job wrote.
        job.function = nullptr;
        m_jobsRun.fetch_add(1, std::memory_order_relaxed);
        if (job.counter->m_pending.fetch_sub(1, std::memory_order_acq_rel) == 1)
        {
            // Taking the lock orders this notification after a waiter's check of the counter, so the
            // waiter cannot miss it between checking and going to sleep.
            {
                const std::scoped_lock lock(m_mutex);
            }
            m_jobFinished.notify_all();
        }
    }

    void JobSystem::wait(JobCounter& counter)
    {
        Job job;
        while (!counter.done())
        {
            if (tryPop(job))
            {
                execute(job);
                continue;
            }
            // Nothing queued: the group's last jobs are running on other threads.
            std::unique_lock lock(m_mutex);
            m_jobFinished.wait(lock, [&] { return counter.done() || !m_queue.empty(); });
        }
    }

    void JobSystem::workerLoop(uint32_t index)
    {
        const std::string name = std::format("Worker {}", index);
        profiler::setThreadName(name.c_str());

        Job job;
        while (true)
        {
            {
                std::unique_lock lock(m_mutex);
                m_jobAvailable.wait(lock, [&] { return m_stopping || !m_queue.empty(); });
                // Jobs still queued at shutdown run first, so their counters reach zero.
                if (m_queue.empty())
                    return;
                job = std::move(m_queue.front());
                m_queue.pop_front();
            }
            execute(job);
        }
    }
}
