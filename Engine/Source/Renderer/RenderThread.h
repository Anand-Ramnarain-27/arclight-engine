#pragma once

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>

namespace arc
{
    /// A thread that runs one piece of work at a time, handed over by the main thread: the
    /// Renderer's render thread, which records and submits a frame while the main thread updates
    /// the next. The thread that submits work is the only one that waits for it.
    class RenderThread
    {
    public:
        /// Starts the thread, named `name` in Tracy.
        explicit RenderThread(const char* name);
        /// Waits for the work in progress, then joins the thread.
        ~RenderThread();

        RenderThread(const RenderThread&) = delete;
        RenderThread& operator=(const RenderThread&) = delete;

        /// Hands `work` to the thread. The previous work must have been waited for.
        void submit(std::function<void()> work);

        /// Blocks until the submitted work has finished. Returns at once when there is none. Once it
        /// returns, everything the work wrote is visible to the caller, and the thread touches
        /// nothing until the next submit().
        void wait();

        /// True on the thread itself.
        [[nodiscard]] bool isCurrent() const { return std::this_thread::get_id() == m_thread.get_id(); }

        /// How long the last finished work took, in milliseconds. Read after wait().
        [[nodiscard]] double lastWorkMilliseconds() const { return m_lastWorkMs; }

    private:
        void loop(const char* name);

        std::mutex m_mutex;
        std::condition_variable m_workReady;
        std::condition_variable m_workDone;
        std::function<void()> m_work;
        bool m_busy = false;
        bool m_stopping = false;
        double m_lastWorkMs = 0.0;
        // Last, so the members above exist before the thread starts using them.
        std::thread m_thread;
    };
}
