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

# TODO: Install Game/Assets once the editor loads files from it (Phase 2, glTF and textures).

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
