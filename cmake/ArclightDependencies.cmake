# Every dependency is pinned to an exact release and SHA-256 hash so builds are reproducible.
include(FetchContent)

# Static, so there is no SDL3.dll to deploy next to the executable.
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON  CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_Declare(SDL3
    URL      https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz
    URL_HASH SHA256=9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(SDL3)

foreach(sdl_target IN ITEMS SDL3-static SDL_uclibc)
    if(TARGET ${sdl_target})
        set_target_properties(${sdl_target} PROPERTIES FOLDER "ThirdParty")
    endif()
endforeach()
