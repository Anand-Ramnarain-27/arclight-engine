#pragma once

union SDL_Event;

namespace arc
{
    /// Engine-internal hook for systems that need raw OS events, such as the ImGui backend.
    /// Listeners receive every event before Input does, in registration order.
    class PlatformEventListener
    {
    public:
        virtual ~PlatformEventListener() = default;

        virtual void onPlatformEvent(const SDL_Event& event) = 0;
    };
}
