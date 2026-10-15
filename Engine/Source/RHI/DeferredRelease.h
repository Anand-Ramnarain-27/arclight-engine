#pragma once

#include <cstdint>
#include <utility>
#include <vector>

namespace arc::rhi
{
    /// Keeps objects alive until the GPU can no longer be using them.
    ///
    /// Each object is tagged with a point on the GPU timeline (on D3D12, a fence value) and is
    /// destroyed once the GPU has passed that point. An object retired while a frame is being
    /// recorded can still be used by that frame, whose fence value is not known until it is
    /// submitted, so it waits untagged until stampOpenFrame() is called at submission.
    /// Not thread-safe.
    template <typename T>
    class DeferredReleaseQueue
    {
    public:
        /// `fence` is the point after which the GPU no longer uses the object.
        void retire(T object, uint64_t fence) { m_tagged.push_back({fence, std::move(object)}); }

        /// For objects the frame being recorded may use.
        void retireAfterOpenFrame(T object) { m_openFrame.push_back(std::move(object)); }

        /// Called when the frame being recorded is submitted, with the fence value it signals.
        void stampOpenFrame(uint64_t fence)
        {
            for (T& object : m_openFrame)
                m_tagged.push_back({fence, std::move(object)});
            m_openFrame.clear();
        }

        /// Destroys every object whose fence value the GPU has reached.
        void release(uint64_t completedFence)
        {
            std::erase_if(m_tagged, [completedFence](const Entry& entry) { return entry.fence <= completedFence; });
        }

        /// Only valid once the GPU is idle.
        void releaseAll()
        {
            m_tagged.clear();
            m_openFrame.clear();
        }

        [[nodiscard]] size_t size() const { return m_tagged.size() + m_openFrame.size(); }

    private:
        struct Entry
        {
            uint64_t fence = 0;
            T object;
        };

        std::vector<Entry> m_tagged;
        std::vector<T> m_openFrame;
    };
}
