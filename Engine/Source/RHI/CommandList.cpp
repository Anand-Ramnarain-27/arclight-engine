#include "RHI/CommandList.h"

#include "RHI/Buffer.h"

#include <algorithm>

namespace arc::rhi
{
    void CommandList::decayBufferStates()
    {
        for (Buffer* buffer : m_trackedBuffers)
            buffer->setState(ResourceState::Common);
        m_trackedBuffers.clear();
    }

    void CommandList::trackBufferState(Buffer& buffer, ResourceState state)
    {
        if (buffer.state() == ResourceState::Common &&
            std::find(m_trackedBuffers.begin(), m_trackedBuffers.end(), &buffer) == m_trackedBuffers.end())
            m_trackedBuffers.push_back(&buffer);
        buffer.setState(state);
    }
}
