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
    Source/Application/Module.h
    Source/Application/Application.h
    Source/Application/Application.cpp
    Source/UI/ImGuiModule.h
    Source/UI/ImGuiModule.cpp
)

target_include_directories(ArclightEngine PUBLIC Source)
target_compile_definitions(ArclightEngine PUBLIC ARCLIGHT_VERSION="${PROJECT_VERSION}")

# PRIVATE: no engine header includes SDL or ImGui, so neither leaks to engine users through the engine.
target_link_libraries(ArclightEngine PRIVATE SDL3::SDL3-static imgui)

arclight_set_warnings(ArclightEngine)

get_target_property(engine_sources ArclightEngine SOURCES)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/Source FILES ${engine_sources})
set_target_properties(ArclightEngine PROPERTIES FOLDER "Arclight")
