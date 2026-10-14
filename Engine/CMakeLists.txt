add_library(ArclightEngine STATIC
    Source/Core/Platform.h
    Source/Core/Assert.h
    Source/Core/Assert.cpp
    Source/Core/Log.h
    Source/Core/Log.cpp
    Source/Core/Time.h
    Source/Core/Time.cpp
    Source/Math/Math.h
    Source/Math/MathUtils.h
    Source/Math/Vector.h
    Source/Math/Quaternion.h
    Source/Math/Quaternion.cpp
    Source/Math/Matrix.h
    Source/Math/Matrix.cpp
    Source/Platform/Window.h
    Source/Platform/Window.cpp
    Source/Platform/Input.h
    Source/Platform/Input.cpp
    Source/Platform/PlatformEvents.h
    Source/Platform/SystemInfo.h
    Source/Platform/SystemInfo.cpp
    Source/RHI/RHITypes.h
    Source/RHI/Texture.h
    Source/RHI/CommandList.h
    Source/RHI/Swapchain.h
    Source/RHI/Device.h
    Source/RHI/Device.cpp
    Source/RHI/Backends.h
    Source/Renderer/Renderer.h
    Source/Renderer/Renderer.cpp
    Source/Application/Module.h
    Source/Application/Application.h
    Source/Application/Application.cpp
    Source/UI/ImGuiModule.h
    Source/UI/ImGuiModule.cpp
    Source/UI/ImGuiRenderBackend.h
)

# One RHI backend per platform. The Null backend lets the engine build and run headless
# elsewhere (Linux compile checks); it is not a shipping target.
if(WIN32)
    target_sources(ArclightEngine PRIVATE
        Source/RHI/D3D12/D3D12Common.h
        Source/RHI/D3D12/D3D12Common.cpp
        Source/RHI/D3D12/D3D12Backend.h
        Source/RHI/D3D12/D3D12Device.cpp
        Source/RHI/D3D12/D3D12Swapchain.cpp
        Source/RHI/D3D12/D3D12CommandList.cpp
        Source/UI/ImGuiRenderBackend_D3D12.cpp
    )
    target_link_libraries(ArclightEngine PRIVATE d3d12 dxgi dxguid)
elseif(APPLE)
    target_sources(ArclightEngine PRIVATE
        Source/RHI/Metal/MetalBackend.h
        Source/RHI/Metal/MetalDevice.mm
        Source/RHI/Metal/MetalSwapchain.mm
        Source/RHI/Metal/MetalCommandList.mm
        Source/UI/ImGuiRenderBackend_Metal.mm
    )
    target_link_libraries(ArclightEngine PRIVATE "-framework Metal" "-framework QuartzCore" "-framework Foundation")
    target_compile_options(ArclightEngine PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
else()
    target_sources(ArclightEngine PRIVATE
        Source/RHI/Null/NullBackend.cpp
        Source/UI/ImGuiRenderBackend_Null.cpp
    )
endif()

target_include_directories(ArclightEngine PUBLIC Source)
target_compile_definitions(ArclightEngine PUBLIC ARCLIGHT_VERSION="${PROJECT_VERSION}")

# PRIVATE: no engine header includes SDL, ImGui or a graphics API header, so none of them
# leak to engine users through the engine.
target_link_libraries(ArclightEngine PRIVATE SDL3::SDL3-static imgui)

arclight_set_warnings(ArclightEngine)

get_target_property(engine_sources ArclightEngine SOURCES)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/Source FILES ${engine_sources})
set_target_properties(ArclightEngine PROPERTIES FOLDER "Arclight")
