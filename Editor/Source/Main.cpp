#include "Application/Application.h"
#include "EditorModule.h"
#include "UI/ImGuiModule.h"

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

    // ImGuiModule must come first: it starts the ImGui frame that EditorModule draws into.
    app.addModule<arc::ImGuiModule>();
    app.addModule<arc::EditorModule>();

    return app.run();
}
