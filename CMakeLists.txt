# Splinter Cell Double Agent (original Xbox), statically recompiled

cmake_minimum_required(VERSION 3.25)
if(WIN32)
    project(scda LANGUAGES C CXX RC)
elseif(APPLE)
    project(scda LANGUAGES C CXX OBJCXX)
else()
    project(scda LANGUAGES C CXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
set(ZYDIS_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(ZYDIS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ZYDIS_BUILD_DOXYGEN OFF CACHE BOOL "" FORCE)
FetchContent_Declare(zydis GIT_REPOSITORY https://github.com/zyantific/zydis.git GIT_TAG v4.1.1 GIT_SHALLOW TRUE)
FetchContent_Declare(tomlplusplus GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git GIT_TAG v3.4.0 GIT_SHALLOW TRUE)
FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG v1.91.0 GIT_SHALLOW TRUE)
# SDL3 for controllers and the headset's audio, static so it links into the exe
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_RENDER OFF CACHE BOOL "" FORCE)
set(SDL_GPU OFF CACHE BOOL "" FORCE)
set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3 GIT_REPOSITORY https://github.com/libsdl-org/SDL.git GIT_TAG release-3.4.16 GIT_SHALLOW TRUE)
# Opus for the wideband voice link, static, without the stack protector and fortify flags that need GCC's libssp
set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(OPUS_STACK_PROTECTOR OFF CACHE BOOL "" FORCE)
set(OPUS_FORTIFY_SOURCE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(opus GIT_REPOSITORY https://github.com/xiph/opus.git GIT_TAG v1.5.2 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(zydis tomlplusplus imgui SDL3 opus)

# x86 to C++ recompiler
file(GLOB RECOMP_SOURCES CONFIGURE_DEPENDS tools/recomp/*.cpp)
add_executable(xbe_recomp ${RECOMP_SOURCES})
target_include_directories(xbe_recomp PRIVATE src tools/recomp)
target_compile_definitions(xbe_recomp PRIVATE _CRT_SECURE_NO_WARNINGS)
target_link_libraries(xbe_recomp PRIVATE Zydis tomlplusplus::tomlplusplus)

# pulls default.xbe and sc4_adv.xbe off a disc image, before anything can be recompiled
add_executable(extract_xbe tools/extract_xbe.cpp src/kernel/disc.cpp)
target_include_directories(extract_xbe PRIVATE src)

if(APPLE)
    set(IMGUI_SOURCES
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp)
else()
    set(IMGUI_SOURCES
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_dx12.cpp)
endif()

file(GLOB HOST_SOURCES CONFIGURE_DEPENDS src/*/*.cpp src/gpu/vulkan/*.cpp)
list(FILTER HOST_SOURCES EXCLUDE REGEX "/src/(game|versus)/")

# SDL's and Opus's headers
if(APPLE)
    set_source_files_properties(
        src/core/input.cpp
        src/audio/voice.cpp
        src/audio/voice_link.cpp
        src/audio/output_sdl3.cpp
        src/app/main.cpp
        src/gpu/vulkan/vulkan.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
        PROPERTIES
        INCLUDE_DIRECTORIES "${sdl3_SOURCE_DIR}/include;${opus_SOURCE_DIR}/include;${imgui_SOURCE_DIR};/opt/homebrew/include")
    set_property(SOURCE src/app/main.cpp APPEND PROPERTY COMPILE_FLAGS "-x objective-c++")
    set_property(SOURCE ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp APPEND PROPERTY COMPILE_DEFINITIONS "SDL_DISABLE_OLD_NAMES=1" "SDL_TRUE=true" "SDL_FALSE=false")
else()
    set_source_files_properties(src/core/input.cpp src/audio/voice.cpp src/audio/voice_link.cpp PROPERTIES
        INCLUDE_DIRECTORIES "${sdl3_SOURCE_DIR}/include;${opus_SOURCE_DIR}/include")
endif()

file(GLOB RECOMP_CONFIGS CONFIGURE_DEPENDS config/*.toml)

# Generate build_info.h and track it as a dependency of version-related
find_package(Git QUIET)
set(BUILD_INFO ${CMAKE_CURRENT_BINARY_DIR}/build_info.h)
add_custom_target(build_info
    COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_CURRENT_SOURCE_DIR} -DOUT=${BUILD_INFO} -DGIT=${GIT_EXECUTABLE}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/build_info.cmake
    BYPRODUCTS ${BUILD_INFO}
    VERBATIM)
if(WIN32)
    set_source_files_properties(src/app/system_info.cpp src/app/version.rc PROPERTIES
        INCLUDE_DIRECTORIES ${CMAKE_CURRENT_BINARY_DIR} OBJECT_DEPENDS ${BUILD_INFO})
    set(EXTRA_EXE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/app/version.rc)
else()
    set_source_files_properties(src/app/system_info.cpp PROPERTIES
        INCLUDE_DIRECTORIES ${CMAKE_CURRENT_BINARY_DIR} OBJECT_DEPENDS ${BUILD_INFO})
    set(EXTRA_EXE_SOURCES)
endif()

# One executable per recompiled image: the image's code from its config (partitions as that config sets them),
# the shared host, and the game hooks only that image has.
function(scda_image target xbe config partitions game_dir as_bundle)
    set(GEN ${CMAKE_CURRENT_SOURCE_DIR}/generated/${game_dir})
    set(GEN_SOURCES ${GEN}/x86_func_mapping.cpp)
    math(EXPR last "${partitions} - 1")
    foreach(i RANGE ${last})
        list(APPEND GEN_SOURCES ${GEN}/x86_recomp.${i}.cpp)
    endforeach()
    add_custom_command(
        OUTPUT ${GEN}/codegen.stamp
        BYPRODUCTS ${GEN_SOURCES} ${GEN}/x86_recomp_shared.h ${GEN}/image_symbols.h
        COMMAND xbe_recomp config/${config}
        COMMAND ${CMAKE_COMMAND} -E touch ${GEN}/codegen.stamp
        DEPENDS xbe_recomp ${RECOMP_CONFIGS} ${CMAKE_CURRENT_SOURCE_DIR}/assets/${xbe}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Recompiling ${xbe}"
        VERBATIM)
    add_custom_target(recompile_${target} DEPENDS ${GEN}/codegen.stamp)
    set_source_files_properties(${GEN_SOURCES} PROPERTIES GENERATED TRUE
        COMPILE_OPTIONS "-O2;-fno-strict-aliasing;-Wno-unused-label;-Wno-parentheses-equality;-Wno-unused-variable")
    file(GLOB GAME_SOURCES CONFIGURE_DEPENDS src/${game_dir}/*.cpp)
    # the xbe's SHA-1: the executable checks the game it finds is the release it was made from
    set(xbe_file ${CMAKE_CURRENT_SOURCE_DIR}/assets/${xbe})
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${xbe_file})
    # the xbe only exists after extract_xbe has run on the disc image, so configuring first is not an error
    if(EXISTS ${xbe_file})
        file(SHA1 ${xbe_file} xbe_sha1)
    else()
        set(xbe_sha1 "")
        message(WARNING "assets/${xbe} is missing: run build-tools/extract_xbe on the disc image, then re-run CMake")
    endif()
    set(XBE_HASH ${CMAKE_CURRENT_BINARY_DIR}/${target}_xbe.cpp)
    file(CONFIGURE OUTPUT ${XBE_HASH} CONTENT "extern const char kImageSha1[] = \"${xbe_sha1}\";\n")
    if(WIN32)
        add_executable(${target} WIN32 ${HOST_SOURCES} ${GAME_SOURCES} ${GEN_SOURCES} ${IMGUI_SOURCES} ${XBE_HASH} ${EXTRA_EXE_SOURCES})
    elseif(APPLE AND as_bundle)
        add_executable(${target} MACOSX_BUNDLE ${HOST_SOURCES} ${GAME_SOURCES} ${GEN_SOURCES} ${IMGUI_SOURCES} ${XBE_HASH})
    else()
        add_executable(${target} ${HOST_SOURCES} ${GAME_SOURCES} ${GEN_SOURCES} ${IMGUI_SOURCES} ${XBE_HASH})
    endif()
    if(APPLE AND as_bundle)
        # the player gets one app: the story build is the bundle, tools/bundle_macos.sh packs it
        set_target_properties(${target} PROPERTIES
            OUTPUT_NAME "Splinter Cell Double Agent"
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/tools/Info.plist")
    endif()
    add_dependencies(${target} recompile_${target} build_info)
    target_include_directories(${target} PRIVATE src ${GEN} /opt/homebrew/include)
    target_include_directories(${target} SYSTEM PRIVATE ${imgui_SOURCE_DIR})
    target_compile_options(${target} PRIVATE -fno-strict-aliasing)
    if(WIN32)
        target_compile_definitions(${target} PRIVATE UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)
        target_link_libraries(${target} PRIVATE tomlplusplus::tomlplusplus $<LINK_ONLY:SDL3::SDL3-static> $<LINK_ONLY:opus> d3d12 dxgi dxguid d3dcompiler xaudio2 ole32 windowscodecs dbghelp winmm dwmapi ws2_32)
        # lld keeps the recompiled weak functions overridable by the host ones
        target_link_options(${target} PRIVATE -fuse-ld=lld -g)
    elseif(APPLE)
        target_compile_definitions(${target} PRIVATE UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS SDL_DISABLE_OLD_NAMES=1 SDL_TRUE=true SDL_FALSE=false ENABLE_HLSL)
        target_link_directories(${target} PRIVATE /opt/homebrew/lib)
        target_link_libraries(${target} PRIVATE
            tomlplusplus::tomlplusplus
            SDL3::SDL3-static
            opus
            vulkan
            MoltenVK
            glslang
            SPIRV
            glslang-default-resource-limits
            "-framework Cocoa"
            "-framework IOKit"
            "-framework AudioToolbox"
            "-framework QuartzCore"
            "-framework Metal"
        )
        # install_name_tool rewrites the Homebrew paths; leave room for the longer @rpath ones
        target_link_options(${target} PRIVATE -g -Wl,-headerpad_max_install_names)
    endif()
endfunction()

scda_image(SCDA default.xbe scda_v2.toml 128 game TRUE)        # story and co-op, the app bundle
scda_image(SCDA_Versus sc4_adv.xbe sc4_adv.toml 64 versus FALSE)  # versus, launched from inside the bundle

if(APPLE)
    # package the story build as a self-contained app: game image, versus build and its libraries inside
    add_dependencies(SCDA SCDA_Versus)
    add_custom_command(TARGET SCDA POST_BUILD
        COMMAND /bin/bash "${CMAKE_CURRENT_SOURCE_DIR}/tools/bundle_macos.sh"
                "$<TARGET_BUNDLE_DIR:SCDA>"
                "$<TARGET_FILE:SCDA>"
                "$<TARGET_FILE:SCDA_Versus>"
                "${CMAKE_CURRENT_SOURCE_DIR}/assets"
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/AppIcon.icns"
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/Info.plist"
        COMMENT "Packaging Splinter Cell Double Agent.app"
        VERBATIM)
endif()
