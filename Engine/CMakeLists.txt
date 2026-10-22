add_library(ArclightEngine STATIC
    Source/Core/Platform.h
    Source/Core/Assert.h
    Source/Core/BinaryStream.h
    Source/Core/Hash.h
    Source/Core/Handle.h
    Source/Core/RangeAllocator.h
    Source/Core/RangeAllocator.cpp
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
    Source/Math/Aabb.h
    Source/Math/Ray.h
    Source/Math/Frustum.h
    Source/Math/Frustum.cpp
    Source/Math/Ray.cpp
    Source/Platform/Window.h
    Source/Platform/Window.cpp
    Source/Platform/Input.h
    Source/Platform/Input.cpp
    Source/Platform/PlatformEvents.h
    Source/Platform/SystemInfo.h
    Source/Platform/SystemInfo.cpp
    Source/Platform/FileDialog.h
    Source/Platform/FileDialog.cpp
    Source/Platform/FileSource.h
    Source/Platform/FileSource.cpp
    Source/Platform/FileSystem.h
    Source/Platform/FileSystem.cpp
    Source/Platform/VirtualFileSystem.h
    Source/Platform/VirtualFileSystem.cpp
    Source/RHI/RHITypes.h
    Source/RHI/Texture.h
    Source/RHI/Sampler.h
    Source/RHI/Buffer.h
    Source/RHI/Shader.h
    Source/RHI/ShaderUtils.h
    Source/RHI/ShaderUtils.cpp
    Source/RHI/DeferredRelease.h
    Source/RHI/BufferUtils.h
    Source/RHI/BufferUtils.cpp
    Source/RHI/UploadRing.h
    Source/RHI/UploadRing.cpp
    Source/RHI/TextureUtils.h
    Source/RHI/TextureUtils.cpp
    Source/RHI/CommandList.h
    Source/RHI/CommandList.cpp
    Source/RHI/Swapchain.h
    Source/RHI/Device.h
    Source/RHI/Device.cpp
    Source/RHI/Backends.h
    Source/RHI/Null/NullBackend.cpp
    Source/Renderer/Renderer.h
    Source/Renderer/Renderer.cpp
    Source/Renderer/Camera.h
    Source/Renderer/Camera.cpp
    Source/Renderer/ModelData.h
    Source/Renderer/GltfImporter.h
    Source/Renderer/GltfImporter.cpp
    Source/Renderer/GeometryPool.h
    Source/Renderer/GeometryPool.cpp
    Source/Renderer/Model.h
    Source/Renderer/Model.cpp
    Source/Renderer/LodGenerator.h
    Source/Renderer/LodGenerator.cpp
    Source/Renderer/CubeMap.h
    Source/Renderer/CubeMap.cpp
    Source/Renderer/Skybox.h
    Source/Renderer/Skybox.cpp
    Source/Renderer/RenderList.h
    Source/Renderer/RenderList.cpp
    Source/Renderer/MeshRenderer.h
    Source/Renderer/MeshRenderer.cpp
    Source/Renderer/ShaderLibrary.h
    Source/Renderer/ShaderLibrary.cpp
    Source/Renderer/ShaderConstants.h
    Source/Renderer/Image.h
    Source/Renderer/Image.cpp
    Source/Renderer/TextureLoader.h
    Source/Renderer/TextureLoader.cpp
    Source/Scene/Uuid.h
    Source/Scene/Uuid.cpp
    Source/Scene/Component.h
    Source/Scene/Transform.h
    Source/Scene/Transform.cpp
    Source/Scene/GameObject.h
    Source/Scene/GameObject.cpp
    Source/Scene/Scene.h
    Source/Scene/Scene.cpp
    Source/Scene/MeshComponent.h
    Source/Scene/MeshComponent.cpp
    Source/Scene/Raycast.h
    Source/Scene/Raycast.cpp
    Source/Scene/ModelInstantiation.h
    Source/Scene/ModelInstantiation.cpp
    Source/Scene/SceneModule.h
    Source/Scene/SceneModule.cpp
    Source/Assets/AssetDatabase.h
    Source/Assets/AssetDatabase.cpp
    Source/Assets/AssetImporters.h
    Source/Assets/AssetImporters.cpp
    Source/Assets/AssetMeta.h
    Source/Assets/AssetMeta.cpp
    Source/Assets/AssetModule.h
    Source/Assets/AssetModule.cpp
    Source/Assets/CookedFormats.h
    Source/Assets/CookedFormats.cpp
    Source/Assets/ResourceManager.h
    Source/Assets/ResourceManager.cpp
    Source/Application/Module.h
    Source/Application/Application.h
    Source/Application/Application.cpp
    Source/UI/ImGuiModule.h
    Source/UI/ImGuiModule.cpp
    Source/UI/ImGuiRenderBackend.h
)

# One GPU backend per platform. The Null backend is always built: it is the device on
# platforms without a GPU backend (Linux compile checks) and is used by the unit tests.
if(WIN32)
    target_sources(ArclightEngine PRIVATE
        Source/RHI/D3D12/D3D12Common.h
        Source/RHI/D3D12/D3D12Common.cpp
        Source/RHI/D3D12/D3D12Backend.h
        Source/RHI/D3D12/D3D12Device.cpp
        Source/RHI/D3D12/D3D12Descriptors.cpp
        Source/RHI/D3D12/D3D12Swapchain.cpp
        Source/RHI/D3D12/D3D12CommandList.cpp
        Source/UI/ImGuiRenderBackend_D3D12.cpp
    )
    target_link_libraries(ArclightEngine PRIVATE d3d12 dxgi dxguid)
    target_include_directories(ArclightEngine SYSTEM PRIVATE ${renderdoc_api_SOURCE_DIR})
elseif(APPLE)
    target_sources(ArclightEngine PRIVATE
        Source/RHI/Metal/MetalBackend.h
        Source/RHI/Metal/MetalDevice.mm
        Source/RHI/Metal/MetalSwapchain.mm
        Source/RHI/Metal/MetalCommandList.mm
        Source/UI/ImGuiRenderBackend_Metal.mm
    )
    target_link_libraries(ArclightEngine PRIVATE "-framework Metal" "-framework QuartzCore" "-framework Foundation")
    # ARC for the Objective-C++ sources. The Xcode generator drops per-language compile options
    # for OBJCXX, so Xcode builds get ARC from its own build setting instead.
    target_compile_options(ArclightEngine PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    set_target_properties(ArclightEngine PROPERTIES XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES)
else()
    target_sources(ArclightEngine PRIVATE
        Source/UI/ImGuiRenderBackend_Null.cpp
    )
endif()

target_include_directories(ArclightEngine PUBLIC Source)
target_compile_definitions(ArclightEngine PUBLIC ARCLIGHT_VERSION="${PROJECT_VERSION}")

# PRIVATE: no engine header includes SDL, ImGui or a graphics API header, so none of them
# leak to engine users through the engine.
target_link_libraries(ArclightEngine PRIVATE SDL3::SDL3-static imgui stb_image tinygltf meshoptimizer)

arclight_set_warnings(ArclightEngine)

arclight_compile_shader(SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/Shaders/Textured.hlsl VERTEX VSMain PIXEL PSMain)
arclight_compile_shader(SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/Shaders/Grid.hlsl VERTEX VSMain PIXEL PSMain)
arclight_compile_shader(SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/Shaders/Mesh.hlsl VERTEX VSMain PIXEL PSMain)
arclight_compile_shader(SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/Shaders/SelectionMask.hlsl VERTEX VSMain PIXEL PSMain)
arclight_compile_shader(SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/Shaders/Outline.hlsl VERTEX VSMain PIXEL PSMain)
arclight_compile_shader(SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/Shaders/Skybox.hlsl VERTEX VSMain PIXEL PSMain)

get_target_property(engine_sources ArclightEngine SOURCES)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/Source FILES ${engine_sources})
set_target_properties(ArclightEngine PROPERTIES FOLDER "Arclight")
