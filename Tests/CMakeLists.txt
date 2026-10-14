add_executable(ArclightTests
    Source/TestMain.cpp
    Source/MathTests.cpp
    Source/TimeTests.cpp
)

target_link_libraries(ArclightTests PRIVATE ArclightEngine doctest::doctest)
arclight_set_warnings(ArclightTests)
set_target_properties(ArclightTests PROPERTIES FOLDER "Arclight")

add_test(NAME ArclightTests COMMAND ArclightTests)
