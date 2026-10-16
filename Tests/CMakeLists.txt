add_executable(ArclightTests
    Source/TestMain.cpp
    Source/HandleTests.cpp
    Source/ResourceLifetimeTests.cpp
    Source/UploadRingTests.cpp
    Source/MathTests.cpp
    Source/CameraTests.cpp
    Source/TimeTests.cpp
    Source/RHITests.cpp
    Source/TextureTests.cpp
    Source/ImageTests.cpp
)

target_link_libraries(ArclightTests PRIVATE ArclightEngine doctest::doctest)
arclight_set_warnings(ArclightTests)
set_target_properties(ArclightTests PROPERTIES FOLDER "Arclight")

add_test(NAME ArclightTests COMMAND ArclightTests)
