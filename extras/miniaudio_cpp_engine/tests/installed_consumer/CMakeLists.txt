cmake_minimum_required(VERSION 3.20)
project(miniaudio_cpp_engine_installed_consumer LANGUAGES CXX)

if(NOT IS_ABSOLUTE "${MINIAUDIO_TEST_INSTALL_PREFIX}")
    message(FATAL_ERROR "MINIAUDIO_TEST_INSTALL_PREFIX must name the absolute staged install")
endif()

add_executable(installed_consumer main.cpp)
target_compile_features(installed_consumer PRIVATE cxx_std_20)
set_target_properties(installed_consumer PROPERTIES CXX_EXTENSIONS OFF)
if(WIN32)
    # No pkg-config dependency is imposed on MSVC consumers.
    find_path(_audio_include NAMES miniaudio.h
        PATHS "${MINIAUDIO_TEST_INSTALL_PREFIX}/include/miniaudio" NO_DEFAULT_PATH REQUIRED)
    find_library(_engine_archive NAMES miniaudio_cpp_engine
        PATHS "${MINIAUDIO_TEST_INSTALL_PREFIX}/lib" NO_DEFAULT_PATH REQUIRED)
    find_library(_audio_archive NAMES miniaudio
        PATHS "${MINIAUDIO_TEST_INSTALL_PREFIX}/lib" NO_DEFAULT_PATH REQUIRED)
    target_include_directories(installed_consumer PRIVATE "${_audio_include}")
    target_link_libraries(installed_consumer PRIVATE "${_engine_archive}" "${_audio_archive}")
else()
    # Scope discovery to this process and exercise the installed static metadata.
    set(ENV{PKG_CONFIG_PATH}
        "${MINIAUDIO_TEST_INSTALL_PREFIX}/lib/pkgconfig:${MINIAUDIO_TEST_INSTALL_PREFIX}/lib64/pkgconfig")
    set(PKG_CONFIG_ARGN --static)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(CPP_ENGINE REQUIRED IMPORTED_TARGET miniaudio-cpp-engine)
    target_link_libraries(installed_consumer PRIVATE PkgConfig::CPP_ENGINE)
endif()

enable_testing()
add_test(NAME installed_consumer COMMAND installed_consumer)
