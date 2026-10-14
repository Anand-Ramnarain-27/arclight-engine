#include "Application/Application.h"

// SDL_main.h is deliberately not included: a standard main() is the entry point on every platform.
int main(int /*argc*/, char* /*argv*/[])
{
    arc::Application app({
        .window = {
            .title = "Arclight",
            .width = 1600,
            .height = 900,
        },
    });

    return app.run();
}
