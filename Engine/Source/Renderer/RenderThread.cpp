#include "Renderer/RenderThread.h"

#include "Core/Assert.h"
#include "Core/Profiler.h"
#include "Core/Time.h"

namespace arc
{
    RenderThread::RenderThread(const char* name)
        : m_thread([this, name] { loop(name); })
    {
    }

    RenderThread::~RenderThread()
    {
        wait();
        {
            const std::scoped_lock lock(m_mutex);
            m_stopping = true;
        }
        m_workReady.notify_one();
        m_thread.join();
    }

    void RenderThread::submit(std::function<void()> work)
    {
        {
            const std::scoped_lock lock(m_mutex);
            ARC_ASSERT(!m_busy, "Work handed to the render thread before the previous work was waited for");
            m_work = std::move(work);
            m_busy = true;
        }
        m_workReady.notify_one();
    }

    void RenderThread::wait()
    {
        std::unique_lock lock(m_mutex);
        m_workDone.wait(lock, [this] { return !m_busy; });
    }

    void RenderThread::loop(const char* name)
    {
        profiler::setThreadName(name);
        while (true)
        {
            std::function<void()> work;
            {
                std::unique_lock lock(m_mutex);
                m_workReady.wait(lock, [this] { return m_busy || m_stopping; });
                if (!m_busy)
                    return;
                work = std::move(m_work);
            }

            const Stopwatch timer;
            work();
            // Destroyed here rather than by the next submit(), so what it captured is released
            // before the main thread is told the work is done.
            work = nullptr;
            const double milliseconds = timer.elapsedMs();
            {
                const std::scoped_lock lock(m_mutex);
                m_lastWorkMs = milliseconds;
                m_busy = false;
            }
            m_workDone.notify_all();
        }
    }
}
