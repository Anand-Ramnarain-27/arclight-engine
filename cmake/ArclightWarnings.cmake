# Applied to Arclight targets only, so third-party warnings stay out of our build output.
function(arclight_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4
            /permissive-
            /Zc:preprocessor  # Required for __VA_OPT__ in the logging and assert macros.
            /Zc:__cplusplus   # Otherwise __cplusplus always reports 199711L.
            /utf-8
            /MP
        )
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic
            -Wshadow
            -Wundef           # Catches misspelled ARC_PLATFORM_* / ARC_COMPILER_* checks.
            -Wnon-virtual-dtor
            -Wold-style-cast
            -Woverloaded-virtual
        )
    endif()
endfunction()
