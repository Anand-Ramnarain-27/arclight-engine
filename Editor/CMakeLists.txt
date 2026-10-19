add_executable(Arclight
    Source/Main.cpp
    Source/EditorModule.h
    Source/EditorModule.cpp
    Source/EditorSelection.h
    Source/EditorSelection.cpp
    Source/Panels/Panel.h
    Source/Panels/ConsolePanel.h
    Source/Panels/ConsolePanel.cpp
    Source/Panels/HierarchyPanel.h
    Source/Panels/HierarchyPanel.cpp
    Source/Panels/InspectorPanel.h
    Source/Panels/InspectorPanel.cpp
    Source/Panels/PerformancePanel.h
    Source/Panels/PerformancePanel.cpp
    Source/Panels/PlaybackPanel.h
    Source/Panels/PlaybackPanel.cpp
    Source/Panels/ScenePanel.h
    Source/Panels/ScenePanel.cpp
    Source/Panels/SystemInfoPanel.h
    Source/Panels/SystemInfoPanel.cpp
    Source/Scene/EditorCamera.h
    Source/Scene/EditorCamera.cpp
    Source/Scene/EditorGrid.h
    Source/Scene/EditorGrid.cpp
    Source/Scene/TextureDemoModule.h
    Source/Scene/TextureDemoModule.cpp
)

target_include_directories(Arclight PRIVATE Source)
target_link_libraries(Arclight PRIVATE ArclightEngine imgui)
arclight_set_warnings(Arclight)

# Compiled shaders are loaded from a Shaders/ folder next to the executable, which
# ArclightShaders writes into bin/ (bin/<Config>/ for multi-config generators).
add_dependencies(Arclight ArclightShaders)

# The sample model is downloaded at configure time (see ArclightDependencies.cmake), not committed,
# and placed in Assets/ next to the executable, the second place findAssetPath() looks.
add_custom_command(TARGET Arclight POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ARCLIGHT_SAMPLE_MODEL}"
            "$<TARGET_FILE_DIR:Arclight>/Assets/Models/Lantern.glb"
    VERBATIM
)
set_target_properties(Arclight PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Release builds use the Windows GUI subsystem so no console window opens next to the editor.
# main() stays the entry point on every platform, so the CRT is told to call it instead of WinMain().
set_target_properties(Arclight PROPERTIES WIN32_EXECUTABLE $<CONFIG:Release>)
if(MSVC)
    target_link_options(Arclight PRIVATE $<$<CONFIG:Release>:/ENTRY:mainCRTStartup>)
endif()

get_target_property(editor_sources Arclight SOURCES)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/Source FILES ${editor_sources})

# Assets and imgui.ini are relative to Game/, so both IDEs launch from there.
set_target_properties(Arclight PROPERTIES
    FOLDER "Arclight"
    VS_DEBUGGER_WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/Game"
    XCODE_GENERATE_SCHEME ON
    XCODE_SCHEME_WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/Game"
    # Metal API validation, the equivalent of the D3D12 debug layer, and GPU frame capture
    # from the Xcode debug bar.
    XCODE_SCHEME_ENABLE_GPU_API_VALIDATION ON
    XCODE_SCHEME_ENABLE_GPU_FRAME_CAPTURE_MODE Metal
)
