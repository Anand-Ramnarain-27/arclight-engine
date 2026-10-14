add_executable(Arclight
    Source/Main.cpp
    Source/EditorModule.h
    Source/EditorModule.cpp
    Source/Panels/Panel.h
    Source/Panels/ConsolePanel.h
    Source/Panels/ConsolePanel.cpp
    Source/Panels/PerformancePanel.h
    Source/Panels/PerformancePanel.cpp
    Source/Panels/PlaybackPanel.h
    Source/Panels/PlaybackPanel.cpp
    Source/Panels/SystemInfoPanel.h
    Source/Panels/SystemInfoPanel.cpp
)

target_include_directories(Arclight PRIVATE Source)
target_link_libraries(Arclight PRIVATE ArclightEngine imgui)
arclight_set_warnings(Arclight)

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
)
