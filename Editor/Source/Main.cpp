#include "Application/Application.h"
#include "EditorModule.h"
#include "Scene/TextureDemoModule.h"
#include "UI/ImGuiModule.h"

#include <string_view>
#include <utility>

// SDL_main.h is deliberately not included: a standard main() is the entry point on every platform.
//
// Options:
//   --pix              Load PIX's GPU capturer (Windows); disables the D3D12 debug layer.
//   --gpu-validation   Enable D3D12 GPU-based validation (slow).
int main(int argc, char* argv[])
{
    arc::ApplicationDesc desc{
        .window = {
            .title = "Arclight",
            .width = 1600,
            .height = 900,
        },
    };
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg = argv[i];
        if (arg == "--pix")
            desc.device.pixCapture = true;
        else if (arg == "--gpu-validation")
            desc.device.gpuValidation = true;
    }

    arc::Application app(std::move(desc));

    // ImGuiModule must come first: it starts the ImGui frame that the others draw into. The
    // EditorModule's dock space must exist before other modules submit windows, and the overlays
    // are drawn over everything the modules before them put in the scene.
    arc::ImGuiModule& imgui = app.addModule<arc::ImGuiModule>();
    arc::EditorModule& editor = app.addModule<arc::EditorModule>(imgui);
    app.addModule<arc::TextureDemoModule>();
    app.addModule<arc::EditorOverlayModule>(editor);

    return app.run();
}
