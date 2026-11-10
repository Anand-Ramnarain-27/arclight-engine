#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "Core/JobSystem.h"

int main(int argc, char** argv)
{
    // Engine code under test takes its parallel paths, on a fixed number of workers whatever the
    // machine, as it does in the editor.
    arc::JobSystem jobs(3);
    arc::setActiveJobSystem(&jobs);

    doctest::Context context(argc, argv);
    const int result = context.run();
    arc::setActiveJobSystem(nullptr);
    return result;
}
