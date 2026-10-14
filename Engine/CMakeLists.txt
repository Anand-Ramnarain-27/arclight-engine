add_library(ArclightEngine STATIC
    Source/Core/Platform.h
    Source/Core/Assert.h
    Source/Core/Assert.cpp
    Source/Core/Log.h
    Source/Core/Log.cpp
    Source/Core/Time.h
    Source/Core/Time.cpp
    Source/Platform/Window.h
    Source/Platform/Window.cpp
    Source/Platform/Input.h
    Source/Platform/Input.cpp
    Source/Application/Module.h
    Source/Application/Application.h
    Source/Application/Application.cpp
)

target_include_directories(ArclightEngine PUBLIC Source)

# PRIVATE: no engine header includes SDL, so SDL types never reach engine users.
target_link_libraries(ArclightEngine PRIVATE SDL3::SDL3-static)

arclight_set_warnings(ArclightEngine)

get_target_property(engine_sources ArclightEngine SOURCES)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/Source FILES ${engine_sources})
set_target_properties(ArclightEngine PROPERTIES FOLDER "Arclight")
