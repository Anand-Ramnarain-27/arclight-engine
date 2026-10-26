# bc7enc_rdo's encoders, built from the files ArclightDependencies.cmake downloads into
# ${bc7enc_dir}. Their own folder so the configuration flags can differ from the rest of the build:
# block encoding is a tight search loop that runs tens of times slower unoptimised, which would make
# cooking a model take minutes in a Debug editor. Flags set here apply to this folder only.
if(MSVC)
    # /RTC1 cannot be combined with optimisation.
    string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    string(REPLACE "/Od" "/O2" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    string(REPLACE "/Ob0" "/Ob2" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
else()
    string(APPEND CMAKE_CXX_FLAGS_DEBUG " -O2")
endif()

add_library(bc7enc STATIC "${bc7enc_dir}/bc7enc.cpp" "${bc7enc_dir}/bc7decomp.cpp" "${bc7enc_dir}/rgbcx.cpp")
target_include_directories(bc7enc SYSTEM PUBLIC "${bc7enc_dir}")
set_target_properties(bc7enc PROPERTIES FOLDER "ThirdParty")
if(MSVC)
    target_compile_options(bc7enc PRIVATE /W0)
else()
    target_compile_options(bc7enc PRIVATE -w)
endif()
