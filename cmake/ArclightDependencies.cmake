# Every dependency is pinned to an exact release so builds are reproducible.
# Archives are also verified by SHA-256; git dependencies are pinned to a release tag.
include(FetchContent)

# Static, so there is no SDL3.dll to deploy next to the executable.
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON  CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
# Keeps SDL's headers and static library out of the Arclight release package.
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(SDL3
    URL      https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz
    URL_HASH SHA256=9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

# Docking branch: required for the dockable editor layout.
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.92.9b-docking
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(SDL3 imgui)

# RenderDoc's in-app API is a single MIT-licensed header; the library itself is injected by
# RenderDoc at launch, so nothing is linked. Windows only: RenderDoc does not support Metal.
if(WIN32)
    FetchContent_Declare(renderdoc_api
        URL      https://raw.githubusercontent.com/baldurk/renderdoc/v1.46/renderdoc/api/app/renderdoc_app.h
        URL_HASH SHA256=b7005e7dc34c3635046868bbd76d81b9b055aede0f56daa0bd39fedee0639ffb
        DOWNLOAD_NO_EXTRACT TRUE
    )
    FetchContent_MakeAvailable(renderdoc_api)
endif()

# stb_image: a single public-domain header (MIT as an alternative), pinned to a commit.
FetchContent_Declare(stb_image
    URL      https://raw.githubusercontent.com/nothings/stb/2c980bb59875b0d32144a71867fbdebb2f77cd20/stb_image.h
    URL_HASH SHA256=594c2fe35d49488b4382dbfaec8f98366defca819d916ac95becf3e75f4200b3
    DOWNLOAD_NO_EXTRACT TRUE
)
FetchContent_MakeAvailable(stb_image)

# The implementation goes in its own target so Arclight's warning flags never see it.
set(stb_image_impl "${CMAKE_BINARY_DIR}/Generated/stb_image.c")
file(CONFIGURE OUTPUT "${stb_image_impl}" CONTENT "#define STB_IMAGE_IMPLEMENTATION\n#include <stb_image.h>\n")
add_library(stb_image STATIC "${stb_image_impl}")
target_include_directories(stb_image SYSTEM PUBLIC ${stb_image_SOURCE_DIR})
# Files are read with SDL_LoadFile (UTF-8 paths on every platform), so stb's own file I/O is left out.
target_compile_definitions(stb_image
    PUBLIC  STBI_NO_STDIO
    PRIVATE STBI_ONLY_PNG STBI_ONLY_JPEG STBI_ONLY_TGA STBI_ONLY_BMP STBI_ONLY_HDR
)
set_target_properties(stb_image PROPERTIES FOLDER "ThirdParty")

# tinygltf: glTF 2.0 parsing (MIT), with nlohmann/json vendored. Its own CMake build is skipped
# (SOURCE_SUBDIR names a folder without a CMakeLists.txt) so the implementation can be compiled here
# with Arclight's options: images are decoded by Arclight's stb_image, and files are read through
# SDL, which handles UTF-8 paths on Windows.
FetchContent_Declare(tinygltf
    GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
    GIT_TAG        v2.9.7
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  no-cmake
)
FetchContent_MakeAvailable(tinygltf)

set(tinygltf_impl "${CMAKE_BINARY_DIR}/Generated/tiny_gltf.cpp")
file(CONFIGURE OUTPUT "${tinygltf_impl}" CONTENT "#define TINYGLTF_IMPLEMENTATION\n#include <tiny_gltf.h>\n")
add_library(tinygltf STATIC "${tinygltf_impl}")
target_include_directories(tinygltf SYSTEM PUBLIC ${tinygltf_SOURCE_DIR})
# Every user of tiny_gltf.h must see the same options as the implementation.
target_compile_definitions(tinygltf PUBLIC TINYGLTF_NO_STB_IMAGE TINYGLTF_NO_STB_IMAGE_WRITE TINYGLTF_NO_FS)
set_target_properties(tinygltf PROPERTIES FOLDER "ThirdParty")

# meshoptimizer: mesh simplification for the generated levels of detail (MIT). Only the library is
# built; its demo and gltfpack are off by default.
# Keeps its headers and static library out of the Arclight release package.
set(MESHOPT_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(meshoptimizer
    GIT_REPOSITORY https://github.com/zeux/meshoptimizer.git
    GIT_TAG        v1.3
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(meshoptimizer)
set_target_properties(meshoptimizer PROPERTIES FOLDER "ThirdParty")

# MikkTSpace: tangent frames for normal maps (zlib), the generator glTF names for files without
# tangents, so normal maps baked by any tool that uses it come out as they were baked. Two C files
# and no build of its own; pinned to a commit.
FetchContent_Declare(mikktspace
    GIT_REPOSITORY https://github.com/mmikk/MikkTSpace.git
    GIT_TAG        3e895b49d05ea07e4c2133156cfa94369e19e409
)
FetchContent_MakeAvailable(mikktspace)
add_library(mikktspace STATIC ${mikktspace_SOURCE_DIR}/mikktspace.c)
target_include_directories(mikktspace SYSTEM PUBLIC ${mikktspace_SOURCE_DIR})
set_target_properties(mikktspace PROPERTIES FOLDER "ThirdParty")

# bc7enc_rdo (MIT or public domain): Rich Geldreich's BC1 to BC5 (rgbcx) and BC7 (bc7enc) block
# encoders and decoders, used to cook textures. Only the eight source files the encoders need are
# downloaded, each checked against its hash, from a pinned commit; the repository also holds a
# prebuilt ISPC compiler and test tools.
set(bc7enc_commit b9438627eef73a1157e84201b6fa6eb2ffd6d9f0)
set(bc7enc_dir "${FETCHCONTENT_BASE_DIR}/bc7enc_rdo")
set(bc7enc_files
    "bc7enc.cpp=b90444a091530a13c61fa6375583fcc9bc9ca47e474fd71f818876de68c1b581"
    "bc7enc.h=f40f4a956ed6a8845b7909e83831de09512424408b32ca29551bf49aee2fecf6"
    "bc7decomp.cpp=cfbe156926ebd96443b4407a9da1b5b7a424fdf989fedca3cf2fd0515e0c368a"
    "bc7decomp.h=cbba8778640e0021f55ca686b6bc19e6960032539084c70af698922b05c5b242"
    "rgbcx.cpp=059243ce8e8e3ec634bc1e3da3996115aaf36196a8d75a24d3dbb3c8bfe0ca98"
    "rgbcx.h=111bd4c0211eca172bec9d757e2e597a47c4e4731bb122c9e8761dca85711bba"
    "rgbcx_table4.h=5051cf4ce17cb1f9d011ef788b7c8d174f2270532da0514c9f394830bab72f36"
    "rgbcx_table4_small.h=d484802656fb7f2249674263bbb14399ab9ccb2a997ac0030e51b17a4bf8595f"
    "LICENSE=b3e843763e8d3bdeb0f469ecd4107fc32b5fa27ebee1af10d2066bc339292800"
)
foreach(entry IN LISTS bc7enc_files)
    string(REPLACE "=" ";" entry "${entry}")
    list(GET entry 0 file_name)
    list(GET entry 1 file_hash)
    set(destination "${bc7enc_dir}/${file_name}")
    if(EXISTS "${destination}")
        file(SHA256 "${destination}" existing_hash)
    else()
        set(existing_hash "")
    endif()
    if(NOT existing_hash STREQUAL file_hash)
        file(DOWNLOAD "https://raw.githubusercontent.com/richgel999/bc7enc_rdo/${bc7enc_commit}/${file_name}"
             "${destination}" EXPECTED_HASH SHA256=${file_hash} STATUS download_status)
        list(GET download_status 0 download_code)
        if(NOT download_code EQUAL 0)
            message(FATAL_ERROR "Could not download bc7enc_rdo's ${file_name}: ${download_status}")
        endif()
    endif()
endforeach()
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/bc7enc" "${CMAKE_BINARY_DIR}/bc7enc")

# The sample model the editor opens at start-up: "Lantern" from the Khronos glTF sample assets,
# CC0 (Microsoft), pinned to a commit of the repository.
FetchContent_Declare(sample_lantern
    URL      https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/edc7c9e67c639d230715049ee31f9a96a6babbbe/Models/Lantern/glTF-Binary/Lantern.glb
    URL_HASH SHA256=a79458c4b02d695187a952f23a63b8bf278e7bc3d316a3c2a314f2d6974181f1
    DOWNLOAD_NO_EXTRACT TRUE
)
FetchContent_MakeAvailable(sample_lantern)
set(ARCLIGHT_SAMPLE_MODEL "${sample_lantern_SOURCE_DIR}/Lantern.glb")

foreach(sdl_target IN ITEMS SDL3-static SDL_uclibc)
    if(TARGET ${sdl_target})
        set_target_properties(${sdl_target} PROPERTIES FOLDER "ThirdParty")
    endif()
endforeach()

# Dear ImGui ships no CMake build of its own.
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
)
# SYSTEM: our strict warning flags must not fire on third-party headers we include.
target_include_directories(imgui SYSTEM PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC SDL3::SDL3-static)

# The renderer backend matching the platform's RHI backend.
if(WIN32)
    target_sources(imgui PRIVATE ${imgui_SOURCE_DIR}/backends/imgui_impl_dx12.cpp)
    # d3dcompiler: the DX12 backend compiles its two small shaders at start-up.
    target_link_libraries(imgui PUBLIC d3d12 dxgi d3dcompiler)
elseif(APPLE)
    target_sources(imgui PRIVATE ${imgui_SOURCE_DIR}/backends/imgui_impl_metal.mm)
    # See Engine/CMakeLists.txt: the Xcode generator needs its own ARC setting.
    target_compile_options(imgui PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    set_target_properties(imgui PROPERTIES XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES)
    target_link_libraries(imgui PUBLIC "-framework Metal" "-framework QuartzCore" "-framework Cocoa")
endif()
target_compile_definitions(imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
set_target_properties(imgui PROPERTIES FOLDER "ThirdParty")

# ImGuizmo: translate / rotate / scale gizmos drawn with ImGui. Pinned to a commit: the last tagged
# release (1.10) still calls the ImDrawList::AddRect() overload that ImGui 1.92.8 deleted. Only
# ImGuizmo.cpp is built; the repository's other widgets are not used.
FetchContent_Declare(imguizmo
    GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
    GIT_TAG        105e4fde45e4e74aae8973d98750820ef7aca7e2
    SOURCE_SUBDIR  no-cmake
)
FetchContent_MakeAvailable(imguizmo)
add_library(imguizmo STATIC ${imguizmo_SOURCE_DIR}/src/ImGuizmo.cpp)
target_include_directories(imguizmo SYSTEM PUBLIC ${imguizmo_SOURCE_DIR}/src)
target_link_libraries(imguizmo PUBLIC imgui)
set_target_properties(imguizmo PROPERTIES FOLDER "ThirdParty")

if(ARCLIGHT_BUILD_TESTS)
    FetchContent_Declare(doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG        v2.5.3
        GIT_SHALLOW    TRUE
    )
    set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(doctest)
endif()
