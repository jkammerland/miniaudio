cmake_minimum_required(VERSION 3.10)

# Extract version from miniaudio.h
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/miniaudio.h" MINIAUDIO_HEADER_CONTENTS)
string(REGEX MATCH "#define MA_VERSION_MAJOR[ \t]+([0-9]+)" _major_match "${MINIAUDIO_HEADER_CONTENTS}")
set(MA_VERSION_MAJOR "${CMAKE_MATCH_1}")
string(REGEX MATCH "#define MA_VERSION_MINOR[ \t]+([0-9]+)" _minor_match "${MINIAUDIO_HEADER_CONTENTS}")
set(MA_VERSION_MINOR "${CMAKE_MATCH_1}")
string(REGEX MATCH "#define MA_VERSION_REVISION[ \t]+([0-9]+)" _revision_match "${MINIAUDIO_HEADER_CONTENTS}")
set(MA_VERSION_REVISION "${CMAKE_MATCH_1}")
set(MINIAUDIO_VERSION "${MA_VERSION_MAJOR}.${MA_VERSION_MINOR}.${MA_VERSION_REVISION}")

project(miniaudio VERSION ${MINIAUDIO_VERSION})


# Options
option(MINIAUDIO_BUILD_EXAMPLES                "Build miniaudio examples"            OFF)
option(MINIAUDIO_BUILD_TESTS                   "Build miniaudio tests"               OFF)
option(MINIAUDIO_BUILD_TOOLS                   "Build miniaudio development tools. Leave this disabled unless you know what you're doing. If you enable this and you get build errors, you clearly do not know what you're doing and yet you still enabled this option. Why would you do that?" OFF)
option(MINIAUDIO_FORCE_CXX                     "Force compilation as C++"            OFF)
option(MINIAUDIO_FORCE_C89                     "Force compilation as C89"            OFF)
option(MINIAUDIO_NO_EXTRA_NODES                "Do not build extra node graph nodes" OFF)
option(MINIAUDIO_NO_LIBVORBIS                  "Disable miniaudio_libvorbis"         OFF)
option(MINIAUDIO_NO_LIBOPUS                    "Disable miniaudio_libopus"           OFF)
option(MINIAUDIO_FETCH_LIBVORBIS               "Fetch hash-pinned libogg/libvorbis development dependencies" OFF)
option(MINIAUDIO_FETCH_LIBOPUS                  "Fetch hash-pinned libogg/libopus/opusfile development dependencies" OFF)
option(MINIAUDIO_REQUIRE_LIBVORBIS              "Fail configuration when the optional libvorbis wrapper cannot be built" OFF)
option(MINIAUDIO_REQUIRE_LIBOPUS                "Fail configuration when the optional libopus wrapper cannot be built" OFF)
option(MINIAUDIO_NO_WASAPI                     "Disable the WASAPI backend"          OFF)
option(MINIAUDIO_NO_DSOUND                     "Disable the DirectSound backend"     OFF)
option(MINIAUDIO_NO_WINMM                      "Disable the WinMM backend"           OFF)
option(MINIAUDIO_NO_ALSA                       "Disable the ALSA backend"            OFF)
option(MINIAUDIO_NO_PULSEAUDIO                 "Disable the PulseAudio backend"      OFF)
option(MINIAUDIO_NO_JACK                       "Disable the JACK backend"            OFF)
option(MINIAUDIO_NO_COREAUDIO                  "Disable the CoreAudio backend"       OFF)
option(MINIAUDIO_NO_SNDIO                      "Disable the sndio backend"           OFF)
option(MINIAUDIO_NO_AUDIO4                     "Disable the audio(4) backend"        OFF)
option(MINIAUDIO_NO_OSS                        "Disable the OSS backend"             OFF)
option(MINIAUDIO_NO_AAUDIO                     "Disable the AAudio backend"          OFF)
option(MINIAUDIO_NO_OPENSL                     "Disable the OpenSL|ES backend"       OFF)
option(MINIAUDIO_NO_WEBAUDIO                   "Disable the Web Audio backend"       OFF)
option(MINIAUDIO_NO_CUSTOM                     "Disable support for custom backends" OFF)
option(MINIAUDIO_NO_NULL                       "Disable the null backend"            OFF)
option(MINIAUDIO_ENABLE_ONLY_SPECIFIC_BACKENDS "Only enable specific backends. Backends can be enabled with MINIAUDIO_ENABLE_[BACKEND]." OFF)
option(MINIAUDIO_ENABLE_WASAPI                 "Enable the WASAPI backend"           OFF)
option(MINIAUDIO_ENABLE_DSOUND                 "Enable the DirectSound backend"      OFF)
option(MINIAUDIO_ENABLE_WINMM                  "Enable the WinMM backend"            OFF)
option(MINIAUDIO_ENABLE_ALSA                   "Enable the ALSA backend"             OFF)
option(MINIAUDIO_ENABLE_PULSEAUDIO             "Enable the PulseAudio backend"       OFF)
option(MINIAUDIO_ENABLE_JACK                   "Enable the JACK backend"             OFF)
option(MINIAUDIO_ENABLE_COREAUDIO              "Enable the CoreAudio backend"        OFF)
option(MINIAUDIO_ENABLE_SNDIO                  "Enable the sndio backend"            OFF)
option(MINIAUDIO_ENABLE_AUDIO4                 "Enable the audio(4) backend"         OFF)
option(MINIAUDIO_ENABLE_OSS                    "Enable the OSS backend"              OFF)
option(MINIAUDIO_ENABLE_AAUDIO                 "Enable the AAudio backend"           OFF)
option(MINIAUDIO_ENABLE_OPENSL                 "Enable the OpenSL|ES backend"        OFF)
option(MINIAUDIO_ENABLE_WEBAUDIO               "Enable the Web Audio backend"        OFF)
option(MINIAUDIO_ENABLE_CUSTOM                 "Enable support for custom backends"  OFF)
option(MINIAUDIO_ENABLE_NULL                   "Enable the null backend"             OFF)
option(MINIAUDIO_NO_DECODING                   "Disable decoding APIs"               OFF)
option(MINIAUDIO_NO_ENCODING                   "Disable encoding APIs"               OFF)
option(MINIAUDIO_NO_WAV                        "Disable the built-in WAV decoder"    OFF)
option(MINIAUDIO_NO_FLAC                       "Disable the built-in FLAC decoder"   OFF)
option(MINIAUDIO_NO_MP3                        "Disable the built-in MP3 decoder"    OFF)
option(MINIAUDIO_NO_DEVICEIO                   "Disable audio playback and capture"  OFF)
option(MINIAUDIO_NO_RESOURCE_MANAGER           "Disable the resource manager API"    OFF)
option(MINIAUDIO_NO_NODE_GRAPH                 "Disable the node graph API"          OFF)
option(MINIAUDIO_NO_ENGINE                     "Disable the high-level engine API"   OFF)
option(MINIAUDIO_NO_THREADING                  "Disable threading. Must be used with MINIAUDIO_NO_DEVICEIO." OFF)
option(MINIAUDIO_NO_GENERATION                 "Disable generation APIs such as ma_waveform and ma_noise" OFF)
option(MINIAUDIO_NO_SSE2                       "Disable SSE2 optimizations"          OFF)
option(MINIAUDIO_NO_AVX2                       "Disable AVX2 optimizations"          OFF)
option(MINIAUDIO_NO_NEON                       "Disable NEON optimizations"          OFF)
option(MINIAUDIO_NO_RUNTIME_LINKING            "Disable runtime linking"             OFF)
option(MINIAUDIO_USE_STDINT                    "Use <stdint.h> for sized types"      OFF)
option(MINIAUDIO_DEBUG_OUTPUT                  "Enable stdout debug output"          OFF)
option(MINIAUDIO_INSTALL                       "Enable installation targets"         ON) 

include(GNUInstallDirs)

# Construct compiler options.
set(COMPILE_OPTIONS)

# Store libraries to install
# When installing any header that imports miniaudio.h from a relative path, we
# need to maintain its place in the directory tree so it can find Miniaudio
set(LIBS_TO_INSTALL)


# Special rules for Emscripten.
#
# - MINIAUDIO_FORCE_C89 is not supported.
# - MINIAUDIO_NO_RUNTIME_LINKING must be enabled.
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    set(MINIAUDIO_FORCE_C89 OFF)
    set(MINIAUDIO_NO_RUNTIME_LINKING ON)

    # This is a hack to work around some errors relating to generation of the pkg-config file.
    set(MINIAUDIO_ENABLE_ONLY_SPECIFIC_BACKENDS ON)
    set(MINIAUDIO_ENABLE_WEBAUDIO ON)
endif()


if(MINIAUDIO_FORCE_CXX AND MINIAUDIO_FORCE_C89)
    message(FATAL_ERROR "MINIAUDIO_FORCE_CXX and MINIAUDIO_FORCE_C89 cannot be enabled at the same time.")
endif()

if(MINIAUDIO_FORCE_CXX)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        set(MINIAUDIO_FORCE_CXX_COMPILE_OPTION -x c++)
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        set(MINIAUDIO_FORCE_CXX_COMPILE_OPTION /TP)
    endif()
    message(STATUS "Compiling .c sources as C++")
endif()

# The driver flags make compilers treat the .c extension as C++, but they do
# not change CMake's language model. Mark each miniaudio-owned .c source as
# CXX as well so CXX_STANDARD and CMAKE_CXX_FLAGS reach the generated rules.
function(miniaudio_force_cxx_target target)
    if(MINIAUDIO_FORCE_CXX)
        get_target_property(target_sources ${target} SOURCES)
        set(has_c_source FALSE)
        foreach(source IN LISTS target_sources)
            if(source MATCHES "\\.c$")
                set_source_files_properties("${source}" PROPERTIES LANGUAGE CXX)
                set(has_c_source TRUE)
            endif()
        endforeach()
        if(has_c_source AND MINIAUDIO_FORCE_CXX_COMPILE_OPTION)
            target_compile_options(${target} PRIVATE ${MINIAUDIO_FORCE_CXX_COMPILE_OPTION})
        endif()
    endif()
endfunction()

if(MINIAUDIO_FORCE_C89)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(STATUS "Compiling as C89")
        list(APPEND COMPILE_OPTIONS -std=c89)
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        message(WARNING "MSVC does not support forcing C89. MINIAUDIO_FORCE_C89 ignored.")
    else()
        message(WARNING "MINIAUDIO_FORCE_C89 is enabled but the compiler does not support it. Ignoring.")
    endif()
endif()

# Warnings
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    list(APPEND COMPILE_OPTIONS -Wall -Wextra -Wpedantic)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    #list(APPEND COMPILE_OPTIONS /W4)
endif()


# Construct compiler defines
set(COMPILE_DEFINES)

if(MINIAUDIO_NO_WASAPI)
    list(APPEND COMPILE_DEFINES MA_NO_WASAPI)
endif()
if(MINIAUDIO_NO_DSOUND)
    list(APPEND COMPILE_DEFINES MA_NO_DSOUND)
endif()
if(MINIAUDIO_NO_WINMM)
    list(APPEND COMPILE_DEFINES MA_NO_WINMM)
endif()
if(MINIAUDIO_NO_ALSA)
    list(APPEND COMPILE_DEFINES MA_NO_ALSA)
endif()
if(MINIAUDIO_NO_PULSEAUDIO)
    list(APPEND COMPILE_DEFINES MA_NO_PULSEAUDIO)
endif()
if(MINIAUDIO_NO_JACK)
    list(APPEND COMPILE_DEFINES MA_NO_JACK)
endif()
if(MINIAUDIO_NO_COREAUDIO)
    list(APPEND COMPILE_DEFINES MA_NO_COREAUDIO)
endif()
if(MINIAUDIO_NO_SNDIO)
    list(APPEND COMPILE_DEFINES MA_NO_SNDIO)
endif()
if(MINIAUDIO_NO_AUDIO4)
    list(APPEND COMPILE_DEFINES MA_NO_AUDIO4)
endif()
if(MINIAUDIO_NO_OSS)
    list(APPEND COMPILE_DEFINES MA_NO_OSS)
endif()
if(MINIAUDIO_NO_AAUDIO)
    list(APPEND COMPILE_DEFINES MA_NO_AAUDIO)
endif()
if(MINIAUDIO_NO_OPENSL)
    list(APPEND COMPILE_DEFINES MA_NO_OPENSL)
endif()
if(MINIAUDIO_NO_WEBAUDIO)
    list(APPEND COMPILE_DEFINES MA_NO_WEBAUDIO)
endif()
if(MINIAUDIO_NO_CUSTOM)
    list(APPEND COMPILE_DEFINES MA_NO_CUSTOM)
endif()
if(MINIAUDIO_NO_NULL)
    list(APPEND COMPILE_DEFINES MA_NO_NULL)
endif()
if(MINIAUDIO_ENABLE_ONLY_SPECIFIC_BACKENDS)
    list(APPEND COMPILE_DEFINES MA_ENABLE_ONLY_SPECIFIC_BACKENDS)

    if(MINIAUDIO_ENABLE_WASAPI)
        list(APPEND COMPILE_DEFINES MA_ENABLE_WASAPI)
    endif()
    if(MINIAUDIO_ENABLE_DSOUND)
        list(APPEND COMPILE_DEFINES MA_ENABLE_DSOUND)
    endif()
    if(MINIAUDIO_ENABLE_WINMM)
        list(APPEND COMPILE_DEFINES MA_ENABLE_WINMM)
    endif()
    if(MINIAUDIO_ENABLE_ALSA)
        list(APPEND COMPILE_DEFINES MA_ENABLE_ALSA)
    endif()
    if(MINIAUDIO_ENABLE_PULSEAUDIO)
        list(APPEND COMPILE_DEFINES MA_ENABLE_PULSEAUDIO)
    endif()
    if(MINIAUDIO_ENABLE_JACK)
        list(APPEND COMPILE_DEFINES MA_ENABLE_JACK)
    endif()
    if(MINIAUDIO_ENABLE_COREAUDIO)
        list(APPEND COMPILE_DEFINES MA_ENABLE_COREAUDIO)
    endif()
    if(MINIAUDIO_ENABLE_SNDIO)
        list(APPEND COMPILE_DEFINES MA_ENABLE_SNDIO)
    endif()
    if(MINIAUDIO_ENABLE_AUDIO4)
        list(APPEND COMPILE_DEFINES MA_ENABLE_AUDIO4)
    endif()
    if(MINIAUDIO_ENABLE_OSS)
        list(APPEND COMPILE_DEFINES MA_ENABLE_OSS)
    endif()
    if(MINIAUDIO_ENABLE_AAUDIO)
        list(APPEND COMPILE_DEFINES MA_ENABLE_AAUDIO)
    endif()
    if(MINIAUDIO_ENABLE_OPENSL)
        list(APPEND COMPILE_DEFINES MA_ENABLE_OPENSL)
    endif()
    if(MINIAUDIO_ENABLE_WEBAUDIO)
        list(APPEND COMPILE_DEFINES MA_ENABLE_WEBAUDIO)
    endif()
    if(MINIAUDIO_ENABLE_CUSTOM)
        list(APPEND COMPILE_DEFINES MA_ENABLE_CUSTOM)
    endif()
    if(MINIAUDIO_ENABLE_NULL)
        list(APPEND COMPILE_DEFINES MA_ENABLE_NULL)
    endif()
endif()
if(MINIAUDIO_NO_DECODING)
    list(APPEND COMPILE_DEFINES MA_NO_DECODING)
endif()
if(MINIAUDIO_NO_ENCODING)
    list(APPEND COMPILE_DEFINES MA_NO_ENCODING)
endif()
if(MINIAUDIO_NO_WAV)
    list(APPEND COMPILE_DEFINES MA_NO_WAV)
endif()
if(MINIAUDIO_NO_FLAC)
    list(APPEND COMPILE_DEFINES MA_NO_FLAC)
endif()
if(MINIAUDIO_NO_MP3)
    list(APPEND COMPILE_DEFINES MA_NO_MP3)
endif()
if(MINIAUDIO_NO_DEVICEIO)
    list(APPEND COMPILE_DEFINES MA_NO_DEVICE_IO)
endif()
if(MINIAUDIO_NO_RESOURCE_MANAGER)
    list(APPEND COMPILE_DEFINES MA_NO_RESOURCE_MANAGER)
endif()
if(MINIAUDIO_NO_NODE_GRAPH)
    list(APPEND COMPILE_DEFINES MA_NO_NODE_GRAPH)
endif()
if(MINIAUDIO_NO_ENGINE)
    list(APPEND COMPILE_DEFINES MA_NO_ENGINE)
endif()
if(MINIAUDIO_NO_THREADING)
    list(APPEND COMPILE_DEFINES MA_NO_THREADING)
endif()
if(MINIAUDIO_NO_GENERATION)
    list(APPEND COMPILE_DEFINES MA_NO_GENERATION)
endif()
if(MINIAUDIO_NO_SSE2)
    list(APPEND COMPILE_DEFINES MA_NO_SSE2)
endif()
if(MINIAUDIO_NO_AVX2)
    list(APPEND COMPILE_DEFINES MA_NO_AVX2)
endif()
if(MINIAUDIO_NO_NEON)
    list(APPEND COMPILE_DEFINES MA_NO_NEON)
endif()
if(MINIAUDIO_NO_RUNTIME_LINKING)
    list(APPEND COMPILE_DEFINES MA_NO_RUNTIME_LINKING)
endif()
if(MINIAUDIO_USE_STDINT)
    list(APPEND COMPILE_DEFINES MA_USE_STDINT)
endif()
if(MINIAUDIO_DEBUG_OUTPUT)
    list(APPEND COMPILE_DEFINES MA_DEBUG_OUTPUT)
endif()


# External Libraries
if(MINIAUDIO_FETCH_LIBVORBIS OR MINIAUDIO_FETCH_LIBOPUS)
    if(CMAKE_VERSION VERSION_LESS 3.16)
        message(FATAL_ERROR "Pinned optional decoder dependencies require CMake 3.16 or newer.")
    endif()
    include(cmake/miniaudio_optional_decoders.cmake)
    miniaudio_fetch_optional_decoder_dependencies()
endif()

function(add_libogg_subdirectory)
    if(NOT TARGET ogg)
        if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/external/ogg/CMakeLists.txt)
            message(STATUS "Building libogg from source.")
            add_subdirectory(external/ogg)
        else()
            message(STATUS "libogg not found.")
        endif()
    endif()
endfunction()

function(add_libvorbis_subdirectory)
    if(NOT TARGET vorbis)
        if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/external/vorbis/CMakeLists.txt)
            add_libogg_subdirectory()
            if(TARGET ogg)
                message(STATUS "Building libvorbis from source.")
                add_subdirectory(external/vorbis)
            else()
                message(STATUS "libogg not found. miniaudio_libvorbis will be excluded.")
            endif()
        endif()
    endif()
endfunction()

function(add_libopus_subdirectory)
    if(NOT TARGET opus)
        if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/external/opus/CMakeLists.txt)
            message(STATUS "Building libopus from source.")
            set(OPUS_BUILD_TESTING OFF)
            add_subdirectory(external/opus)
        else()
            message(STATUS "libopus not found. miniaudio_libopus will be excluded.")
        endif()
    endif()
endfunction()

function(add_libopusfile_subdirectory)
    if(NOT TARGET opusfile)
        if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/external/opusfile/CMakeLists.txt)
            add_libogg_subdirectory()
            if(TARGET ogg)
                add_libopus_subdirectory()
                if(TARGET opus)
                    message(STATUS "Building libopusfile from source.")
                    set(OP_DISABLE_HTTP TRUE)
                    set(OP_DISABLE_DOCS TRUE)
                    set(OP_DISABLE_EXAMPLES TRUE)
                    add_subdirectory(external/opusfile)
                else()
                    message(STATUS "libopus not found. miniaudio_libopus will be excluded.")
                endif()
            else()
                message(STATUS "libogg not found. miniaudio_libopus will be excluded.")
            endif()
        endif()
    endif()
endfunction()


# vorbisfile
#
# The vorbisfile target is required for miniaudio_libvorbis. If the vorbisfile target has already been
# defined we'll just use that. Otherwise we'll try to use pkg-config. If that fails, as a last resort
# we'll allow building it from source from the external/vorbis directory.
if(NOT MINIAUDIO_NO_LIBVORBIS)
    if(NOT TARGET vorbisfile)
        # Try pkg-config first
        find_package(PkgConfig QUIET)
        if(PKG_CONFIG_FOUND)
            pkg_check_modules(PC_VORBISFILE vorbisfile)
        endif()

        if(PC_VORBISFILE_FOUND)
            message(STATUS "Found vorbisfile via pkg-config: ${PC_VORBISFILE_LIBRARIES}")
            set(HAS_LIBVORBIS TRUE)
        else()
            # Fallback to building from source.
            add_libvorbis_subdirectory()
            if(NOT TARGET vorbisfile)
                message(STATUS "libvorbisfile not found. miniaudio_libvorbis will be excluded.")
            else()
                set(HAS_LIBVORBIS TRUE)
            endif()
        endif()
    else()
        message(STATUS "libvorbisfile already found.")
        set(HAS_LIBVORBIS TRUE)
    endif()
endif()

# opusfile
#
# This is the same as vorbisfile above, but for opusfile.
if(NOT MINIAUDIO_NO_LIBOPUS)
    if(NOT TARGET opusfile)
        # Try pkg-config first
        find_package(PkgConfig QUIET)
        if(PKG_CONFIG_FOUND)
            pkg_check_modules(PC_OPUSFILE opusfile)
        endif()

        if(PC_OPUSFILE_FOUND)
            message(STATUS "Found opusfile via pkg-config: ${PC_OPUSFILE_LIBRARIES}")
            set(HAS_LIBOPUS TRUE)
        else()
            # Fallback to building from source.
            add_libopusfile_subdirectory()
            if(NOT TARGET opusfile)
                message(STATUS "libopusfile not found. miniaudio_libopus will be excluded.")
            else()
                set(HAS_LIBOPUS TRUE)
            endif()
        endif()
    else()
        message(STATUS "libopusfile already found.")
        set(HAS_LIBOPUS TRUE)
    endif()
endif()

if(MINIAUDIO_REQUIRE_LIBVORBIS AND NOT HAS_LIBVORBIS)
    message(FATAL_ERROR
        "MINIAUDIO_REQUIRE_LIBVORBIS is enabled, but libvorbisfile was not found. "
        "Install it or enable MINIAUDIO_FETCH_LIBVORBIS.")
endif()

if(MINIAUDIO_REQUIRE_LIBOPUS AND NOT HAS_LIBOPUS)
    message(FATAL_ERROR
        "MINIAUDIO_REQUIRE_LIBOPUS is enabled, but opusfile was not found. "
        "Install it or enable MINIAUDIO_FETCH_LIBOPUS.")
endif()


find_library(SDL2_LIBRARY NAMES SDL2)
if(SDL2_LIBRARY)
    message(STATUS "Found SDL2: ${SDL2_LIBRARY}")
    set(HAS_SDL2 TRUE)
else()
    message(STATUS "SDL2 not found. SDL2 examples will be excluded.")
endif()

# SteamAudio has an annoying SDK setup. In the lib folder there is a folder for each platform. We need to specify the
# platform we're compiling for.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64")
    # Assume 64-bit. Now we need to check if it's for Windows or Linux.
    if(WIN32)
        set(STEAMAUDIO_ARCH windows-x64)
    else()
        set(STEAMAUDIO_ARCH linux-x64)
    endif()
else()
    # Assume 32-bit. Now we need to check if it's for Windows or Linux.
    if(WIN32)
        set(STEAMAUDIO_ARCH windows-x86)
    else()
        set(STEAMAUDIO_ARCH linux-x86)
    endif()
endif()

# When searching for SteamAudio, we'll support installing it in the external/steamaudio directory.
set(STEAMAUDIO_FIND_LIBRARY_HINTS)
list(APPEND STEAMAUDIO_FIND_LIBRARY_HINTS ${CMAKE_CURRENT_SOURCE_DIR}/external/steamaudio/lib/${STEAMAUDIO_ARCH})

if(WIN32)
else()
    list(APPEND STEAMAUDIO_FIND_LIBRARY_HINTS /opt/steamaudio/lib/${STEAMAUDIO_ARCH})
    list(APPEND STEAMAUDIO_FIND_LIBRARY_HINTS /usr/local/steamaudio/lib/${STEAMAUDIO_ARCH})
endif()

set(STEAMAUDIO_FIND_HEADER_HINTS)
list(APPEND STEAMAUDIO_FIND_HEADER_HINTS ${CMAKE_CURRENT_SOURCE_DIR}/external/steamaudio/include)

if(WIN32)
else()
    list(APPEND STEAMAUDIO_FIND_HEADER_HINTS /opt/steamaudio/include)
    list(APPEND STEAMAUDIO_FIND_HEADER_HINTS /usr/local/steamaudio/include)
endif()


find_library(STEAMAUDIO_LIBRARY NAMES phonon HINTS ${STEAMAUDIO_FIND_LIBRARY_HINTS})
if(STEAMAUDIO_LIBRARY)
    message(STATUS "Found SteamAudio: ${STEAMAUDIO_LIBRARY}")

    find_path(STEAMAUDIO_INCLUDE_DIR
        NAMES phonon.h
        HINTS ${STEAMAUDIO_FIND_HEADER_HINTS}
    )
    if(STEAMAUDIO_INCLUDE_DIR)
        message(STATUS "Found phonon.h in ${STEAMAUDIO_INCLUDE_DIR}")
        set(HAS_STEAMAUDIO TRUE)
    else()
        message(STATUS "Could not find phonon.h. miniaudio_engine_steamaudio will be excluded.")
    endif()
else()
    message(STATUS "SteamAudio not found. miniaudio_engine_steamaudio will be excluded.")
endif()


# Link libraries
set(COMMON_LINK_LIBRARIES)

if (UNIX)
    if(NOT MINIAUDIO_NO_RUNTIME_LINKING)
        # Not all platforms actually use a separate "dl" library, notably NetBSD and OpenBSD.
        find_library(LIB_DL NAMES dl)
        if(LIB_DL)
            list(APPEND COMMON_LINK_LIBRARIES ${LIB_DL})  # For dlopen(), etc. Most compilers will link to this by default, but some may not.
        endif()
    endif()
    
    find_library(LIB_PTHREAD NAMES pthread)
    if(LIB_PTHREAD)
        list(APPEND COMMON_LINK_LIBRARIES ${LIB_PTHREAD}) # Some compilers will not link to pthread by default so list it here just in case.
    endif()

    find_library(LIB_M NAMES m)
    if(LIB_M)
        list(APPEND COMMON_LINK_LIBRARIES ${LIB_M})
    endif()

    # If we're compiling for 32-bit ARM we need to link to -latomic.
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^arm" AND NOT CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
        find_library(LIB_ATOMIC NAMES atomic)
        if(LIB_ATOMIC)
            list(APPEND COMMON_LINK_LIBRARIES ${LIB_ATOMIC})
        endif()
    endif()
endif()


# Static Libraries
add_library(miniaudio
    miniaudio.c
    miniaudio.h
)
miniaudio_force_cxx_target(miniaudio)

list(APPEND LIBS_TO_INSTALL miniaudio)
if(MINIAUDIO_INSTALL)
    install(FILES miniaudio.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/miniaudio)
endif()

target_include_directories(miniaudio PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options    (miniaudio PRIVATE ${COMPILE_OPTIONS})
target_compile_definitions(miniaudio PRIVATE ${COMPILE_DEFINES})


add_library(libvorbis_interface INTERFACE)
if(HAS_LIBVORBIS)
    if(TARGET vorbisfile)
        target_link_libraries(libvorbis_interface INTERFACE vorbisfile)
    elseif(PC_VORBISFILE_FOUND)
        target_link_libraries     (libvorbis_interface INTERFACE ${PC_VORBISFILE_LIBRARIES})
        target_include_directories(libvorbis_interface INTERFACE ${PC_VORBISFILE_INCLUDE_DIRS})
        target_link_directories   (libvorbis_interface INTERFACE ${PC_VORBISFILE_LIBRARY_DIRS})
        target_compile_options    (libvorbis_interface INTERFACE ${PC_VORBISFILE_CFLAGS_OTHER})
    endif()
endif()

if(HAS_LIBVORBIS)
    add_library(miniaudio_libvorbis
        extras/decoders/libvorbis/miniaudio_libvorbis.c
        extras/decoders/libvorbis/miniaudio_libvorbis.h
    )
    miniaudio_force_cxx_target(miniaudio_libvorbis)

    list(APPEND LIBS_TO_INSTALL miniaudio_libvorbis)
    if(MINIAUDIO_INSTALL)
        install(FILES extras/decoders/libvorbis/miniaudio_libvorbis.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/miniaudio/extras/decoders/libvorbis)
    endif()

    target_compile_options    (miniaudio_libvorbis PRIVATE ${COMPILE_OPTIONS})
    target_compile_definitions(miniaudio_libvorbis PRIVATE ${COMPILE_DEFINES})
    target_link_libraries     (miniaudio_libvorbis PRIVATE libvorbis_interface)
    target_include_directories(miniaudio_libvorbis PUBLIC  extras/decoders/libvorbis/)
endif()


add_library(libopus_interface INTERFACE)
if(HAS_LIBOPUS)
    if(TARGET opusfile)
        target_link_libraries     (libopus_interface INTERFACE opusfile)
    elseif(PC_OPUSFILE_FOUND)
        target_link_libraries     (libopus_interface INTERFACE ${PC_OPUSFILE_LIBRARIES})
        target_include_directories(libopus_interface INTERFACE ${PC_OPUSFILE_INCLUDE_DIRS})
        target_link_directories   (libopus_interface INTERFACE ${PC_OPUSFILE_LIBRARY_DIRS})
        target_compile_options    (libopus_interface INTERFACE ${PC_OPUSFILE_CFLAGS_OTHER})
    endif()
endif()

if(HAS_LIBOPUS)
    add_library(miniaudio_libopus
        extras/decoders/libopus/miniaudio_libopus.c
        extras/decoders/libopus/miniaudio_libopus.h
    )
    miniaudio_force_cxx_target(miniaudio_libopus)


    list(APPEND LIBS_TO_INSTALL miniaudio_libopus)
    if(MINIAUDIO_INSTALL)
        install(FILES extras/decoders/libopus/miniaudio_libopus.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/miniaudio/extras/decoders/libopus)
    endif()

    target_compile_options    (miniaudio_libopus PRIVATE ${COMPILE_OPTIONS})
    target_compile_definitions(miniaudio_libopus PRIVATE ${COMPILE_DEFINES})
    target_link_libraries     (miniaudio_libopus PRIVATE libopus_interface)
    target_include_directories(miniaudio_libopus PUBLIC  extras/decoders/libopus/)
endif()


if (NOT MINIAUDIO_NO_EXTRA_NODES)
    function(add_extra_node name)
        add_library(miniaudio_${name}_node
            extras/nodes/ma_${name}_node/ma_${name}_node.c
            extras/nodes/ma_${name}_node/ma_${name}_node.h
        )
        miniaudio_force_cxx_target(miniaudio_${name}_node)
        
        set(libs "${LIBS_TO_INSTALL}")

        list(APPEND libs miniaudio_${name}_node)
        set(LIBS_TO_INSTALL "${libs}" PARENT_SCOPE) # without PARENT_SCOPE, any changes are lost
        if(MINIAUDIO_INSTALL)
            install(FILES extras/nodes/ma_${name}_node/ma_${name}_node.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/miniaudio/extras/nodes/ma_${name}_node)
        endif()

        target_include_directories(miniaudio_${name}_node PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/extras/nodes/ma_${name}_node)
        target_compile_options    (miniaudio_${name}_node PRIVATE ${COMPILE_OPTIONS})
        target_compile_definitions(miniaudio_${name}_node PRIVATE ${COMPILE_DEFINES})

        if(MINIAUDIO_BUILD_EXAMPLES)
            add_executable(miniaudio_${name}_node_example extras/nodes/ma_${name}_node/ma_${name}_node_example.c)
            miniaudio_force_cxx_target(miniaudio_${name}_node_example)
            target_link_libraries(miniaudio_${name}_node_example PRIVATE miniaudio_common_options)
        endif()
    endfunction()

    add_extra_node(channel_combiner)
    add_extra_node(channel_separator)
    add_extra_node(ltrim)
    add_extra_node(reverb)
    add_extra_node(vocoder)
endif()


# Interface with common options to simplify the setup of tests and examples. Note that we don't pass
# in COMPILE_DEFINES here because want to allow the tests and examples to define their own defines. If
# we were to use COMPILE_DEFINES here many of the tests and examples would not compile.
add_library(miniaudio_common_options INTERFACE)
target_compile_options(miniaudio_common_options INTERFACE ${COMPILE_OPTIONS})
target_link_libraries (miniaudio_common_options INTERFACE ${COMMON_LINK_LIBRARIES})

function(is_backend_enabled NAME)
    if (NOT MINIAUDIO_NO_${NAME} AND (NOT MINIAUDIO_ENABLE_ONLY_SPECIFIC_BACKENDS OR MINIAUDIO_ENABLE_${NAME}))
        set(${NAME}_ENABLED TRUE PARENT_SCOPE)
    else()
        set(${NAME}_ENABLED FALSE PARENT_SCOPE)
    endif()
endfunction()

set(LINKED_LIBS)

if(MINIAUDIO_NO_RUNTIME_LINKING)
    is_backend_enabled(PULSEAUDIO)
    if (PULSEAUDIO_ENABLED)
        find_package(PulseAudio)

        if (PulseAudio_FOUND)
            target_link_libraries(miniaudio PRIVATE ${PULSEAUDIO_LIBRARY})
            target_include_directories(miniaudio SYSTEM PRIVATE ${PULSEAUDIO_INCLUDE_DIR})
            list(APPEND LINKED_LIBS libpulse)
        endif()
    endif()

    is_backend_enabled(ALSA)
    if (ALSA_ENABLED)
        find_package(PkgConfig QUIET)

        if(PKG_CONFIG_FOUND)
            pkg_check_modules(PC_ALSA alsa)
        endif()

        find_library(ALSA_LIBRARY
            NAMES asound
            HINTS ${PC_ALSA_LIBRARY_DIRS}
        )

        if (ALSA_LIBRARY)
            find_path(ALSA_INCLUDE_DIR
                NAMES alsa/asoundlib.h
                HINTS ${PC_ALSA_INCLUDE_DIRS}
            )

            target_link_libraries(miniaudio PRIVATE ${ALSA_LIBRARY})
            target_include_directories(miniaudio PRIVATE ${ALSA_INCLUDE_DIR})
            list(APPEND LINKED_LIBS alsa)
        endif()
    endif()

    is_backend_enabled(SNDIO)
    if (SNDIO_ENABLED)
        find_package(PkgConfig QUIET)

        if(PKG_CONFIG_FOUND)
            pkg_check_modules(PC_SNDIO sndio)
        endif()

        find_library(SNDIO_LIBRARY
            NAMES sndio
            HINTS ${PC_SNDIO_LIBRARY_DIRS}
        )

        if (SNDIO_LIBRARY)
            target_link_libraries(miniaudio PRIVATE ${SNDIO_LIBRARY})
            list(APPEND LINKED_LIBS sndio)
        endif()
    endif()

    is_backend_enabled(JACK)
    if (JACK_ENABLED)
        find_package(PkgConfig QUIET)

        if(PKG_CONFIG_FOUND)
            pkg_check_modules(PC_JACK jack)
        endif()

        find_library(JACK_LIBRARY
            NAMES jack
            HINTS ${PC_JACK_LIBRARY_DIRS}
        )

        if (JACK_LIBRARY)
            find_path(JACK_INCLUDE_DIR
                NAMES jack/jack.h
                HINTS ${PC_JACK_INCLUDE_DIRS}
            )

            target_link_libraries(miniaudio PRIVATE ${JACK_LIBRARY})
            target_include_directories(miniaudio PRIVATE ${JACK_INCLUDE_DIR})
            list(APPEND LINKED_LIBS jack)
        endif()
    endif()
endif()

# Tests
#
# All tests are compiled as a single translation unit. There is no need to add miniaudio as a link library.
if(MINIAUDIO_BUILD_TESTS)
    enable_testing()

    set(TESTS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tests)

    function(add_miniaudio_test name source)
        add_executable(${name} ${TESTS_DIR}/${source})
        miniaudio_force_cxx_target(${name})
        target_link_libraries(${name} PRIVATE miniaudio_common_options)
    endfunction()

    # Disable C++ tests when forcing C89. This is needed because we'll be passing -std=c89 which will cause errors when trying to compile a C++ file.
    if(NOT MINIAUDIO_FORCE_C89)
        # The debugging test is only used for debugging miniaudio itself. Don't do add_test() for this, and do not include it in in any automated testing.
        add_miniaudio_test(miniaudio_debugging debugging/debugging.cpp)

        add_miniaudio_test(miniaudio_cpp cpp/cpp.cpp)
        add_test(NAME miniaudio_cpp COMMAND miniaudio_cpp --auto) # This is just the deviceio test.
    endif()

    add_miniaudio_test(miniaudio_deviceio deviceio/deviceio.c)
    add_test(NAME miniaudio_deviceio COMMAND miniaudio_deviceio --auto)

    add_miniaudio_test(miniaudio_conversion conversion/conversion.c)
    add_test(NAME miniaudio_conversion COMMAND miniaudio_conversion)

    add_miniaudio_test(miniaudio_filtering filtering/filtering.c)
    add_test(NAME miniaudio_filtering COMMAND miniaudio_filtering ${CMAKE_CURRENT_SOURCE_DIR}/data/16-44100-stereo.flac)
    
    add_miniaudio_test(miniaudio_generation generation/generation.c)
    add_test(NAME miniaudio_generation COMMAND miniaudio_generation)

    add_miniaudio_test(miniaudio_hardening_wav hardening/hardening_wav.c)
    add_test(NAME miniaudio_hardening_wav COMMAND miniaudio_hardening_wav)
    set_tests_properties(miniaudio_hardening_wav PROPERTIES TIMEOUT 5)

    add_miniaudio_test(miniaudio_hardening_wav_metadata hardening/hardening_wav_metadata.c)
    add_test(NAME miniaudio_hardening_wav_metadata COMMAND miniaudio_hardening_wav_metadata)

    add_miniaudio_test(miniaudio_hardening_resource_manager hardening/hardening_resource_manager.c)
    add_test(NAME miniaudio_hardening_resource_manager COMMAND miniaudio_hardening_resource_manager)

    add_miniaudio_test(miniaudio_hardening_no_resource_manager hardening/hardening_no_resource_manager.c)
    if(MINIAUDIO_FORCE_CXX AND DEFINED CMAKE_CXX_STANDARD AND CMAKE_CXX_STANDARD GREATER_EQUAL 23)
        target_compile_definitions(miniaudio_hardening_no_resource_manager PRIVATE MINIAUDIO_HARDENING_REQUIRE_CXX23)
    endif()
    add_test(NAME miniaudio_hardening_no_resource_manager COMMAND miniaudio_hardening_no_resource_manager)

    if(WIN32)
        add_miniaudio_test(miniaudio_hardening_fs_win32 hardening/hardening_fs_win32.c)
        target_compile_definitions(miniaudio_hardening_fs_win32 PRIVATE UNICODE _UNICODE)
        add_test(NAME miniaudio_hardening_fs_win32 COMMAND miniaudio_hardening_fs_win32)
    endif()

    add_miniaudio_test(miniaudio_hardening_mp3 hardening/hardening_mp3.c)
    add_test(NAME miniaudio_hardening_mp3 COMMAND miniaudio_hardening_mp3)

    add_miniaudio_test(miniaudio_hardening_gainer hardening/hardening_gainer.c)
    add_test(NAME miniaudio_hardening_gainer COMMAND miniaudio_hardening_gainer)

    add_miniaudio_test(miniaudio_hardening_gainer_scalar hardening/hardening_gainer.c)
    target_compile_definitions(miniaudio_hardening_gainer_scalar PRIVATE MA_NO_SSE2)
    add_test(NAME miniaudio_hardening_gainer_scalar COMMAND miniaudio_hardening_gainer_scalar)

    add_miniaudio_test(miniaudio_hardening_engine_eof hardening/hardening_engine_eof.c)
    add_test(NAME miniaudio_hardening_engine_eof COMMAND miniaudio_hardening_engine_eof)

    add_miniaudio_test(miniaudio_hardening_device_post_init hardening/hardening_device_post_init.c)
    add_test(NAME miniaudio_hardening_device_post_init COMMAND miniaudio_hardening_device_post_init)

    if(HAS_LIBOPUS)
        add_miniaudio_test(miniaudio_hardening_libopus hardening/hardening_libopus.c)
        target_link_libraries(miniaudio_hardening_libopus PRIVATE libopus_interface)
        add_test(NAME miniaudio_hardening_libopus COMMAND miniaudio_hardening_libopus ${CMAKE_CURRENT_SOURCE_DIR}/data/48000-stereo.opus)
        set_tests_properties(miniaudio_hardening_libopus PROPERTIES TIMEOUT 30)
    endif()

    if(HAS_LIBVORBIS)
        add_miniaudio_test(miniaudio_hardening_libvorbis hardening/hardening_libvorbis.c)
        target_link_libraries(miniaudio_hardening_libvorbis PRIVATE libvorbis_interface)
        add_test(NAME miniaudio_hardening_libvorbis COMMAND miniaudio_hardening_libvorbis ${CMAKE_CURRENT_SOURCE_DIR}/data/48000-stereo.ogg)
        set_tests_properties(miniaudio_hardening_libvorbis PROPERTIES TIMEOUT 30)
    endif()

    if(HAS_STEAMAUDIO)
        add_miniaudio_test(miniaudio_hardening_steamaudio hardening/hardening_steamaudio.c)
        target_include_directories(miniaudio_hardening_steamaudio PRIVATE ${STEAMAUDIO_INCLUDE_DIR})
        target_link_libraries(miniaudio_hardening_steamaudio PRIVATE ${STEAMAUDIO_LIBRARY})
        add_test(NAME miniaudio_hardening_steamaudio COMMAND miniaudio_hardening_steamaudio)
        set_tests_properties(miniaudio_hardening_steamaudio PROPERTIES TIMEOUT 30)
        add_miniaudio_test(miniaudio_hardening_steamaudio_teardown hardening/hardening_steamaudio_teardown.c)
        target_include_directories(miniaudio_hardening_steamaudio_teardown PRIVATE ${STEAMAUDIO_INCLUDE_DIR})
        target_link_libraries(miniaudio_hardening_steamaudio_teardown PRIVATE ${STEAMAUDIO_LIBRARY})
        add_test(NAME miniaudio_hardening_steamaudio_teardown COMMAND miniaudio_hardening_steamaudio_teardown)
        set_tests_properties(miniaudio_hardening_steamaudio_teardown PROPERTIES TIMEOUT 30)
        add_miniaudio_test(miniaudio_hardening_steamaudio_direction hardening/hardening_steamaudio_direction.c)
        target_include_directories(miniaudio_hardening_steamaudio_direction PRIVATE ${STEAMAUDIO_INCLUDE_DIR})
        target_link_libraries(miniaudio_hardening_steamaudio_direction PRIVATE ${STEAMAUDIO_LIBRARY})
        add_test(NAME miniaudio_hardening_steamaudio_direction COMMAND miniaudio_hardening_steamaudio_direction)
        set_tests_properties(miniaudio_hardening_steamaudio_direction PROPERTIES TIMEOUT 30)
    endif()

    if(WIN32)
        add_miniaudio_test(miniaudio_hardening_wasapi_lifecycle hardening/hardening_wasapi_lifecycle.c)
        add_test(NAME miniaudio_hardening_wasapi_lifecycle COMMAND miniaudio_hardening_wasapi_lifecycle)

        add_miniaudio_test(miniaudio_hardening_wasapi_process_loopback hardening/hardening_wasapi_process_loopback.c)
        add_test(NAME miniaudio_hardening_wasapi_process_loopback COMMAND miniaudio_hardening_wasapi_process_loopback)
    endif()

    if(APPLE)
        add_miniaudio_test(miniaudio_hardening_coreaudio_lifecycle hardening/hardening_coreaudio_lifecycle.c)
        add_test(NAME miniaudio_hardening_coreaudio_lifecycle COMMAND miniaudio_hardening_coreaudio_lifecycle)
        set_tests_properties(miniaudio_hardening_coreaudio_lifecycle PROPERTIES TIMEOUT 30)
    endif()
endif()

# Examples
#
# Like tests, all examples are compiled as a single translation unit. There is no need to add miniaudio as a link library.
if (MINIAUDIO_BUILD_EXAMPLES)
    set(EXAMPLES_DIR ${CMAKE_CURRENT_SOURCE_DIR}/examples)

    function(add_miniaudio_example name source)
        add_executable(${name} ${EXAMPLES_DIR}/${source})
        miniaudio_force_cxx_target(${name})
        target_link_libraries(${name} PRIVATE miniaudio_common_options)
    endfunction()

    add_miniaudio_example(miniaudio_custom_backend custom_backend.c)

    add_miniaudio_example(miniaudio_custom_decoder_engine custom_decoder_engine.c)
    if(HAS_LIBVORBIS)
        target_link_libraries(miniaudio_custom_decoder_engine PRIVATE libvorbis_interface)
    else()
        target_compile_definitions(miniaudio_custom_decoder_engine PRIVATE MA_NO_LIBVORBIS)
        message(STATUS "miniaudio_libvorbis is disabled. Vorbis support is disabled in miniaudio_custom_decoder_engine.")
    endif()
    if(HAS_LIBOPUS)
    target_link_libraries(miniaudio_custom_decoder_engine PRIVATE libopus_interface)
    else()
        target_compile_definitions(miniaudio_custom_decoder_engine PRIVATE MA_NO_LIBOPUS)
        message(STATUS "miniaudio_libopus is disabled. Opus support is disabled in miniaudio_custom_decoder_engine.")
    endif()

    add_miniaudio_example(miniaudio_custom_decoder custom_decoder.c)
    if(HAS_LIBVORBIS)
        target_link_libraries(miniaudio_custom_decoder PRIVATE libvorbis_interface)
    else()
        target_compile_definitions(miniaudio_custom_decoder PRIVATE MA_NO_LIBVORBIS)
        message(STATUS "miniaudio_libvorbis is disabled. Vorbis support is disabled in miniaudio_custom_decoder.")
    endif()
    if(HAS_LIBOPUS)
        target_link_libraries(miniaudio_custom_decoder PRIVATE libopus_interface)
    else()
        target_compile_definitions(miniaudio_custom_decoder PRIVATE MA_NO_LIBOPUS)
        message(STATUS "miniaudio_libopus is disabled. Opus support is disabled in miniaudio_custom_decoder.")
    endif()

    add_miniaudio_example(miniaudio_data_source_chaining data_source_chaining.c)
    add_miniaudio_example(miniaudio_duplex_effect duplex_effect.c)
    add_miniaudio_example(miniaudio_engine_advanced engine_advanced.c)
    add_miniaudio_example(miniaudio_engine_effects engine_effects.c)
    add_miniaudio_example(miniaudio_engine_hello_world engine_hello_world.c)

    if(HAS_SDL2)
        add_miniaudio_example(miniaudio_engine_sdl engine_sdl.c)
        target_link_libraries(miniaudio_engine_sdl PRIVATE ${SDL2_LIBRARY})
    else()
        message(STATUS "SDL2 could not be found. miniaudio_engine_sdl has been excluded.")
    endif()

    if(HAS_STEAMAUDIO)
        add_miniaudio_example(miniaudio_engine_steamaudio engine_steamaudio.c)
        target_include_directories(miniaudio_engine_steamaudio PRIVATE ${STEAMAUDIO_INCLUDE_DIR})
        target_link_libraries     (miniaudio_engine_steamaudio PRIVATE ${STEAMAUDIO_LIBRARY})
    else()
        message(STATUS "SteamAudio could not be found. miniaudio_engine_steamaudio has been excluded.")
    endif()

    add_miniaudio_example(miniaudio_hilo_interop hilo_interop.c)
    add_miniaudio_example(miniaudio_node_graph node_graph.c)
    add_miniaudio_example(miniaudio_resource_manager_advanced resource_manager_advanced.c)
    add_miniaudio_example(miniaudio_resource_manager resource_manager.c)
    add_miniaudio_example(miniaudio_simple_capture simple_capture.c)
    add_miniaudio_example(miniaudio_simple_duplex simple_duplex.c)
    add_miniaudio_example(miniaudio_simple_enumeration simple_enumeration.c)
    add_miniaudio_example(miniaudio_simple_loopback simple_loopback.c)
    add_miniaudio_example(miniaudio_simple_looping simple_looping.c)
    add_miniaudio_example(miniaudio_simple_mixing simple_mixing.c)
    add_miniaudio_example(miniaudio_simple_playback_sine simple_playback_sine.c)
    add_miniaudio_example(miniaudio_simple_playback simple_playback.c)
    add_miniaudio_example(miniaudio_simple_spatialization simple_spatialization.c)
endif()


# Tools
if (MINIAUDIO_BUILD_TOOLS)
    set(TOOLS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tools)

    add_executable(madoc ${TOOLS_DIR}/madoc/madoc.c)
    miniaudio_force_cxx_target(madoc)
endif()


if(IS_ABSOLUTE "${CMAKE_INSTALL_INCLUDEDIR}")
    set(MINIAUDIO_PC_INCLUDEDIR "${CMAKE_INSTALL_INCLUDEDIR}")
else()
    set(MINIAUDIO_PC_INCLUDEDIR "\${prefix}/${CMAKE_INSTALL_INCLUDEDIR}")
endif()
if(IS_ABSOLUTE "${CMAKE_INSTALL_LIBDIR}")
    set(MINIAUDIO_PC_LIBDIR "${CMAKE_INSTALL_LIBDIR}")
else()
    set(MINIAUDIO_PC_LIBDIR "\${exec_prefix}/${CMAKE_INSTALL_LIBDIR}")
endif()

string(JOIN ", " MINIAUDIO_PC_REQUIRES_PRIVATE ${LINKED_LIBS})

# Add vorbisfile and opusfile to pkg-config dependencies if found via pkg-config
set(PC_REQUIRES_PRIVATE_LIST)
if(PC_VORBISFILE_FOUND AND HAS_LIBVORBIS)
    list(APPEND PC_REQUIRES_PRIVATE_LIST "vorbisfile")
endif()
if(PC_OPUSFILE_FOUND AND HAS_LIBOPUS)
    list(APPEND PC_REQUIRES_PRIVATE_LIST "opusfile")
endif()
if(PC_REQUIRES_PRIVATE_LIST)
    if(MINIAUDIO_PC_REQUIRES_PRIVATE)
        string(APPEND MINIAUDIO_PC_REQUIRES_PRIVATE ", ")
    endif()
    string(JOIN ", " PC_REQUIRES_STR ${PC_REQUIRES_PRIVATE_LIST})
    string(APPEND MINIAUDIO_PC_REQUIRES_PRIVATE "${PC_REQUIRES_STR}")
endif()
list(TRANSFORM COMMON_LINK_LIBRARIES PREPEND "-l")
string(JOIN " " MINIAUDIO_PC_LIBS_PRIVATE ${COMMON_LINK_LIBRARIES})
list(TRANSFORM COMPILE_DEFINES PREPEND "-D")
string(JOIN " " MINIAUDIO_PC_CFLAGS ${COMPILE_DEFINES})

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/miniaudio.pc.in" "${CMAKE_CURRENT_BINARY_DIR}/miniaudio.pc" @ONLY)

if(MINIAUDIO_INSTALL)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/miniaudio.pc" DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")

    message(STATUS "Library list: ${LIBS_TO_INSTALL}")
    install(TARGETS ${LIBS_TO_INSTALL}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()
