#include "Core/Assert.h"
#include "Core/Log.h"

namespace arc::detail
{
    void reportAssertFailure(std::string_view condition, const std::source_location& where,
                             std::string_view message)
    {
        // log::write is called directly so the reported location is the assert site, not this function.
        if (message.empty())
            log::write(log::Level::Fatal, where, std::format("Assertion failed: {}", condition));
        else
            log::write(log::Level::Fatal, where, std::format("Assertion failed: {} - {}", condition, message));
    }
}
