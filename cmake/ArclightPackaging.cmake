# Install layout and CPack settings for the release zip.
# The zip is self-contained: extract it anywhere and run the executable from that folder.

install(TARGETS Arclight
    RUNTIME DESTINATION .
    BUNDLE  DESTINATION .
)

if(ARCLIGHT_SHADERS_ENABLED)
    install(DIRECTORY "${ARCLIGHT_SHADER_OUTPUT_DIR}/" DESTINATION Shaders)
endif()

install(FILES
    ${PROJECT_SOURCE_DIR}/README.md
    ${PROJECT_SOURCE_DIR}/LICENSE
    ${PROJECT_SOURCE_DIR}/CHANGELOG.md
    DESTINATION .
)

# SDL3 and Dear ImGui are linked statically, so their notices ship with the binary.
install(FILES ${sdl3_SOURCE_DIR}/LICENSE.txt  DESTINATION ThirdPartyLicenses RENAME SDL3.txt)
install(FILES ${imgui_SOURCE_DIR}/LICENSE.txt DESTINATION ThirdPartyLicenses RENAME DearImGui.txt)
install(FILES ${tinygltf_SOURCE_DIR}/LICENSE DESTINATION ThirdPartyLicenses RENAME tinygltf.txt)
# nlohmann/json, vendored by tinygltf, carries its MIT notice in the comment that opens json.hpp.
file(READ "${tinygltf_SOURCE_DIR}/json.hpp" nlohmann_json_header LIMIT 4096)
string(FIND "${nlohmann_json_header}" "*/" nlohmann_json_notice_end)
string(SUBSTRING "${nlohmann_json_header}" 0 ${nlohmann_json_notice_end} nlohmann_json_notice)
file(WRITE "${CMAKE_BINARY_DIR}/Generated/nlohmann_json.txt" "${nlohmann_json_notice}*/\n")
install(FILES "${CMAKE_BINARY_DIR}/Generated/nlohmann_json.txt" DESTINATION ThirdPartyLicenses)
# stb_image is public domain or MIT, at the user's choice; its notice closes stb_image.h.
file(READ "${stb_image_SOURCE_DIR}/stb_image.h" stb_image_header)
string(FIND "${stb_image_header}" "This software is available under 2 licenses" stb_image_notice_start)
string(FIND "${stb_image_header}" "*/" stb_image_notice_end REVERSE)
math(EXPR stb_image_notice_length "${stb_image_notice_end} - ${stb_image_notice_start}")
string(SUBSTRING "${stb_image_header}" ${stb_image_notice_start} ${stb_image_notice_length} stb_image_notice)
file(WRITE "${CMAKE_BINARY_DIR}/Generated/stb_image.txt" "${stb_image_notice}")
install(FILES "${CMAKE_BINARY_DIR}/Generated/stb_image.txt" DESTINATION ThirdPartyLicenses)

# findAssetPath() looks for Assets/ in the working directory first (Game/ when run from an IDE),
# then next to the executable, which is where the package puts it.
install(DIRECTORY ${PROJECT_SOURCE_DIR}/Game/Assets/ DESTINATION Assets)
install(FILES "${ARCLIGHT_SAMPLE_MODEL}" DESTINATION Assets/Models)

if(WIN32)
    set(arclight_package_platform "windows-x64")
elseif(APPLE)
    set(arclight_package_platform "macos-arm64")
else()
    string(TOLOWER "${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}" arclight_package_platform)
endif()

set(CPACK_PACKAGE_NAME                "Arclight")
set(CPACK_PACKAGE_VENDOR              "Anand Ramnarain")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_VERSION             "${PROJECT_VERSION}")
set(CPACK_PACKAGE_HOMEPAGE_URL        "https://github.com/Anand-Ramnarain-27/arclight-engine")
set(CPACK_PACKAGE_FILE_NAME           "Arclight-${PROJECT_VERSION}-${arclight_package_platform}")
set(CPACK_GENERATOR                   "ZIP")
# One top-level folder inside the zip, so extracting it never scatters files.
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY  ON)

include(CPack)
