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

    // ImGuiModule must come first: it starts the ImGui frame that EditorModule draws into.
    app.addModule<arc::ImGuiModule>();
    app.addModule<arc::EditorModule>();
    app.addModule<arc::TextureDemoModule>();

    return app.run();
}
