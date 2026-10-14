# Offline shader compilation. Shaders are written once in HLSL and compiled at build time:
#
#   Windows   HLSL --dxc--> DXIL                                          (.dxil)
#   macOS     HLSL --dxc -spirv--> SPIR-V --spirv-cross--> MSL --metal--> (.metallib)
#   Linux     DXIL, SPIR-V and MSL source only, as a compile check (no GPU backend).
#
# Outputs go to a Shaders/ folder next to the editor executable, named <Name>.<stage>.<ext>,
# e.g. Triangle.vs.dxil. Debug builds keep debug information for PIX, RenderDoc and Xcode;
# Release builds are optimised and stripped.

include(FetchContent)

set(ARCLIGHT_SHADER_MODEL "6_0" CACHE STRING "HLSL shader model used for every stage")

# DXC -------------------------------------------------------------------------------------
# Windows uses a pinned official release. Microsoft publishes no macOS build, so on macOS DXC
# comes from the Vulkan SDK (which bundles one) or from ARCLIGHT_DXC.
if(WIN32 AND NOT ARCLIGHT_DXC)
    FetchContent_Declare(dxc
        URL      https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.9.2607/dxc_2026_07_29.zip
        URL_HASH SHA256=a1dfb116ba3eeae6a1582291b53a8e7bf65ad760676bd3194685c8f7367cd241
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(dxc)
    # dxil.dll must sit next to dxc.exe; without it DXC cannot sign the DXIL and D3D12 rejects it.
    set(ARCLIGHT_DXC "${dxc_SOURCE_DIR}/bin/x64/dxc.exe" CACHE FILEPATH "DirectX Shader Compiler" FORCE)
else()
    # VULKAN_SDK is only set in shells that sourced the SDK's setup-env.sh, so the default
    # install location (~/VulkanSDK/<version>/macOS) is searched too, newest version first.
    file(GLOB vulkan_sdk_bins "$ENV{HOME}/VulkanSDK/*/macOS/bin")
    list(SORT vulkan_sdk_bins COMPARE NATURAL ORDER DESCENDING)
    find_program(ARCLIGHT_DXC
        NAMES dxc
        HINTS "$ENV{VULKAN_SDK}/bin" ${vulkan_sdk_bins}
        DOC "DirectX Shader Compiler (built with SPIR-V support)"
    )
endif()

if(ARCLIGHT_DXC)
    set(ARCLIGHT_SHADERS_ENABLED ON)
    message(STATUS "Arclight shaders: DXC at ${ARCLIGHT_DXC}")
elseif(APPLE)
    message(FATAL_ERROR
        "DXC was not found. Install the Vulkan SDK (https://vulkan.lunarg.com/sdk/home#mac), which "
        "includes dxc, then re-run CMake from a terminal where VULKAN_SDK is set, or pass "
        "-DARCLIGHT_DXC=/path/to/dxc.")
else()
    message(WARNING "DXC was not found; shaders will not be compiled. Set ARCLIGHT_DXC to enable them.")
    set(ARCLIGHT_SHADERS_ENABLED OFF)
endif()

# SPIRV-Cross and the Metal compiler ------------------------------------------------------
if(ARCLIGHT_SHADERS_ENABLED AND NOT WIN32)
    set(SPIRV_CROSS_CLI ON CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_SHARED OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_SKIP_INSTALL ON CACHE BOOL "" FORCE)
    FetchContent_Declare(spirv_cross
        GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Cross.git
        GIT_TAG        vulkan-sdk-1.4.363.0
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(spirv_cross)
    foreach(spvc_target IN ITEMS spirv-cross spirv-cross-core spirv-cross-glsl spirv-cross-msl spirv-cross-hlsl
                                 spirv-cross-cpp spirv-cross-reflect spirv-cross-util spirv-cross-c)
        if(TARGET ${spvc_target})
            set_target_properties(${spvc_target} PROPERTIES FOLDER "ThirdParty")
        endif()
    endforeach()
endif()

if(APPLE)
    find_program(ARCLIGHT_XCRUN xcrun REQUIRED)
    execute_process(COMMAND ${ARCLIGHT_XCRUN} -sdk macosx -f metal
                    RESULT_VARIABLE metal_found OUTPUT_QUIET ERROR_QUIET)
    if(NOT metal_found EQUAL 0)
        message(FATAL_ERROR
            "The Metal compiler was not found. In recent Xcode versions it is a separate download: "
            "run 'xcodebuild -downloadComponent MetalToolchain' (or Xcode > Settings > Components).")
    endif()
endif()

# Output location -------------------------------------------------------------------------
get_property(arclight_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(arclight_multi_config)
    set(ARCLIGHT_SHADER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/Shaders")
    set(arclight_shader_intermediate_dir "${CMAKE_BINARY_DIR}/ShaderIntermediate/$<CONFIG>")
else()
    set(ARCLIGHT_SHADER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/bin/Shaders")
    set(arclight_shader_intermediate_dir "${CMAKE_BINARY_DIR}/ShaderIntermediate")
endif()

add_custom_target(ArclightShaders)
set_target_properties(ArclightShaders PROPERTIES FOLDER "Arclight")

# arclight_compile_shader(SOURCE <file.hlsl> [VERTEX <entry>] [PIXEL <entry>])
#
# Compiles each listed stage of one HLSL file. The output name is the file name without its
# extension, so Engine/Shaders/Triangle.hlsl with VERTEX VSMain produces Triangle.vs.<ext>.
function(arclight_compile_shader)
    if(NOT ARCLIGHT_SHADERS_ENABLED)
        return()
    endif()

    cmake_parse_arguments(ARG "" "SOURCE;VERTEX;PIXEL" "" ${ARGN})
    get_filename_component(name "${ARG_SOURCE}" NAME_WE)
    get_filename_component(source_dir "${ARG_SOURCE}" DIRECTORY)
    file(GLOB includes CONFIGURE_DEPENDS "${source_dir}/*.hlsli")

    # Debug: unoptimised with embedded debug info, so PIX and RenderDoc show HLSL source.
    set(common_flags -HV 2021 -WX -I "${source_dir}")
    set(dxil_flags "$<IF:$<CONFIG:Debug>,-Od;-Zi;-Qembed_debug,-O3>")
    set(spirv_flags -spirv -fspv-target-env=vulkan1.1 "$<IF:$<CONFIG:Debug>,-O0,-O3>")
    # Keeps line tables and the MSL source in the metallib for the Xcode shader debugger.
    set(metal_flags "$<$<CONFIG:Debug>:-gline-tables-only;-frecord-sources>")
    set(macos_min "${CMAKE_OSX_DEPLOYMENT_TARGET}")
    if(NOT macos_min)
        set(macos_min 14.0)
    endif()

    set(outputs "")
    foreach(stage IN ITEMS VERTEX PIXEL)
        if(NOT ARG_${stage})
            continue()
        endif()
        set(entry "${ARG_${stage}}")
        if(stage STREQUAL "VERTEX")
            set(short vs)
            set(profile vs_${ARCLIGHT_SHADER_MODEL})
        else()
            set(short ps)
            set(profile ps_${ARCLIGHT_SHADER_MODEL})
        endif()

        set(base "${name}.${short}")
        set(dxil "${ARCLIGHT_SHADER_OUTPUT_DIR}/${base}.dxil")
        set(spirv "${arclight_shader_intermediate_dir}/${base}.spv")
        set(msl "${arclight_shader_intermediate_dir}/${base}.metal")
        set(metallib "${ARCLIGHT_SHADER_OUTPUT_DIR}/${base}.metallib")

        if(NOT APPLE)
            add_custom_command(
                OUTPUT "${dxil}"
                COMMAND ${CMAKE_COMMAND} -E make_directory "${ARCLIGHT_SHADER_OUTPUT_DIR}"
                COMMAND "${ARCLIGHT_DXC}" ${common_flags} -T ${profile} -E ${entry}
                        "${dxil_flags}" -Fo "${dxil}" "${ARG_SOURCE}"
                DEPENDS "${ARG_SOURCE}" ${includes}
                COMMENT "DXC ${base}.dxil"
                COMMAND_EXPAND_LISTS
                VERBATIM
            )
            list(APPEND outputs "${dxil}")
        endif()

        if(NOT WIN32)
            # No -fvk-invert-y: Metal's clip space has +Y up like D3D12, unlike Vulkan's.
            add_custom_command(
                OUTPUT "${spirv}" "${msl}"
                COMMAND ${CMAKE_COMMAND} -E make_directory "${arclight_shader_intermediate_dir}"
                COMMAND "${ARCLIGHT_DXC}" ${common_flags} ${spirv_flags} -T ${profile} -E ${entry}
                        -Fo "${spirv}" "${ARG_SOURCE}"
                COMMAND spirv-cross "${spirv}" --msl --msl-version 30000 --output "${msl}"
                DEPENDS "${ARG_SOURCE}" ${includes} spirv-cross
                COMMENT "DXC + SPIRV-Cross ${base}.metal"
                COMMAND_EXPAND_LISTS
                VERBATIM
            )
            if(APPLE)
                add_custom_command(
                    OUTPUT "${metallib}"
                    COMMAND ${CMAKE_COMMAND} -E make_directory "${ARCLIGHT_SHADER_OUTPUT_DIR}"
                    COMMAND "${ARCLIGHT_XCRUN}" -sdk macosx metal -std=metal3.0
                            -mmacosx-version-min=${macos_min}
                            "${metal_flags}" -o "${metallib}" "${msl}"
                    DEPENDS "${msl}"
                    COMMENT "metal ${base}.metallib"
                    COMMAND_EXPAND_LISTS
                    VERBATIM
                )
                list(APPEND outputs "${metallib}")
            else()
                list(APPEND outputs "${msl}")
            endif()
        endif()
    endforeach()

    # Listed as sources so they show in the IDE. Visual Studio would otherwise compile .hlsl files
    # itself with its built-in FXC rule (entry point "main"), which fails and is not our pipeline.
    set_source_files_properties("${ARG_SOURCE}" ${includes} PROPERTIES VS_TOOL_OVERRIDE "None")
    add_custom_target(ArclightShader_${name} DEPENDS ${outputs} SOURCES "${ARG_SOURCE}" ${includes})
    set_target_properties(ArclightShader_${name} PROPERTIES FOLDER "Arclight/Shaders")
    add_dependencies(ArclightShaders ArclightShader_${name})
endfunction()
