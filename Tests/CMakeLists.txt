add_executable(ArclightTests
    Source/TestMain.cpp
    Source/TestAssets.h
    Source/TestAssets.cpp
    Source/HandleTests.cpp
    Source/ResourceLifetimeTests.cpp
    Source/UploadRingTests.cpp
    Source/MathTests.cpp
    Source/CameraTests.cpp
    Source/GltfTests.cpp
    Source/SceneTests.cpp
    Source/RaycastTests.cpp
    Source/RenderListTests.cpp
    Source/VisibilityTests.cpp
    Source/TimeTests.cpp
    Source/RHITests.cpp
    Source/TextureTests.cpp
    Source/ImageTests.cpp
    Source/FileSystemTests.cpp
    Source/AssetTests.cpp
    Source/SceneSerializerTests.cpp
    Source/LightingTests.cpp
    Source/BrdfTests.cpp
    Source/TangentTests.cpp
    Source/IblTests.cpp
    Source/TextureEncodingTests.cpp
    Source/ToonTests.cpp
)

target_link_libraries(ArclightTests PRIVATE ArclightEngine doctest::doctest)
arclight_set_warnings(ArclightTests)
set_target_properties(ArclightTests PROPERTIES FOLDER "Arclight")

add_test(NAME ArclightTests COMMAND ArclightTests)
