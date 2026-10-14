add_executable(Arclight
    Source/Main.cpp
)

target_link_libraries(Arclight PRIVATE ArclightEngine)
arclight_set_warnings(Arclight)

# TODO: Build Release as a WIN32 (no console) executable with /ENTRY:mainCRTStartup.

# Assets are loaded relative to Game/, so both IDEs launch from there.
set_target_properties(Arclight PROPERTIES
    FOLDER "Arclight"
    VS_DEBUGGER_WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/Game"
    XCODE_GENERATE_SCHEME ON
    XCODE_SCHEME_WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/Game"
)
