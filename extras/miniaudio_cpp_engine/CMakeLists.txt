set(MINIAUDIO_CPP_ENGINE_COMPILE_OPTIONS ${COMPILE_OPTIONS})
list(FILTER MINIAUDIO_CPP_ENGINE_COMPILE_OPTIONS EXCLUDE REGEX "^-std=c89$")

add_library(miniaudio_cpp_engine STATIC
    src/device_adapter.cpp
    src/render_plan.cpp
)
add_library(miniaudio::cpp_engine ALIAS miniaudio_cpp_engine)

target_compile_features(miniaudio_cpp_engine PUBLIC cxx_std_20)
set_target_properties(miniaudio_cpp_engine PROPERTIES
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)
target_include_directories(miniaudio_cpp_engine PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/miniaudio>
)
target_link_libraries(miniaudio_cpp_engine PUBLIC miniaudio ${COMMON_LINK_LIBRARIES})
target_compile_definitions(miniaudio_cpp_engine PRIVATE ${COMPILE_DEFINES})
target_compile_options(miniaudio_cpp_engine PRIVATE ${MINIAUDIO_CPP_ENGINE_COMPILE_OPTIONS})

if(HAS_STEAMAUDIO)
    add_library(miniaudio_cpp_engine_steamaudio STATIC src/steam_audio_binaural.cpp)
    add_library(miniaudio::cpp_engine_steamaudio ALIAS miniaudio_cpp_engine_steamaudio)
    target_compile_features(miniaudio_cpp_engine_steamaudio PUBLIC cxx_std_20)
    set_target_properties(miniaudio_cpp_engine_steamaudio PROPERTIES
        CXX_EXTENSIONS OFF
        POSITION_INDEPENDENT_CODE ON
    )
    target_include_directories(miniaudio_cpp_engine_steamaudio
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            ${STEAMAUDIO_INCLUDE_DIR}
    )
    target_link_libraries(miniaudio_cpp_engine_steamaudio
        PUBLIC miniaudio_cpp_engine
        PRIVATE ${STEAMAUDIO_LIBRARY}
    )
    target_compile_options(miniaudio_cpp_engine_steamaudio PRIVATE ${MINIAUDIO_CPP_ENGINE_COMPILE_OPTIONS})
endif()

set(MINIAUDIO_CPP_ENGINE_TIMER_WHEELS_SOURCE_DIR "" CACHE PATH
    "timer_wheels source checkout used by the experimental integration tests")
set(_tw_pinned_revision "38d087f02dc4fb9f00a36d10fbf5ca97e02e2aa9")
set(_tw_previous_pinned_revisions
    "e9272b5d263859abbe15c4dfd6d123c542e9e9c0"
    "f5853d075ba011efc5bd872ce892f1162d51db8a"
    "17156fe1b395123b32eebc74a87231e8ca454168"
    "0b7a10ef1e84287968ff3354e0560efb8a43f1bf"
    "a46be19e251888a259bbe18051cab4d67920fe65"
    "392cab579f6c6556df16131bea878832a0b585ee")
set(MINIAUDIO_CPP_ENGINE_TIMER_WHEELS_REVISION_OVERRIDE "" CACHE STRING
    "Explicit timer_wheels revision override; empty uses the tested project pin")

# The original option stored the project default in the cache, so an existing
# build directory could retain an earlier project pin after it advanced.
# Migrate known defaults. Preserve custom legacy values as explicit overrides.
if(DEFINED CACHE{MINIAUDIO_CPP_ENGINE_TIMER_WHEELS_REVISION})
    get_property(_tw_legacy_revision CACHE MINIAUDIO_CPP_ENGINE_TIMER_WHEELS_REVISION PROPERTY VALUE)
    if(_tw_legacy_revision IN_LIST _tw_previous_pinned_revisions)
        message(STATUS "Migrating cached timer_wheels project pin from ${_tw_legacy_revision} to ${_tw_pinned_revision}")
    elseif(_tw_legacy_revision STREQUAL _tw_pinned_revision)
        message(STATUS "Removing the legacy cached timer_wheels project pin ${_tw_legacy_revision}")
    elseif(_tw_legacy_revision AND NOT MINIAUDIO_CPP_ENGINE_TIMER_WHEELS_REVISION_OVERRIDE)
        set(MINIAUDIO_CPP_ENGINE_TIMER_WHEELS_REVISION_OVERRIDE
            "${_tw_legacy_revision}" CACHE STRING
            "Explicit timer_wheels revision override; empty uses the tested project pin" FORCE)
        message(DEPRECATION
            "MINIAUDIO_CPP_ENGINE_TIMER_WHEELS_REVISION is now "
            "MINIAUDIO_CPP_ENGINE_TIMER_WHEELS_REVISION_OVERRIDE")
    endif()
    unset(MINIAUDIO_CPP_ENGINE_TIMER_WHEELS_REVISION CACHE)
endif()

set(_tw_expected_revision "${_tw_pinned_revision}")
if(MINIAUDIO_CPP_ENGINE_TIMER_WHEELS_REVISION_OVERRIDE)
    set(_tw_expected_revision "${MINIAUDIO_CPP_ENGINE_TIMER_WHEELS_REVISION_OVERRIDE}")
endif()
option(MINIAUDIO_CPP_ENGINE_VERIFY_TIMER_WHEELS_REVISION
    "Require Git verification of the timer_wheels source revision" ON)

set(MINIAUDIO_CPP_ENGINE_TIMER_WHEELS_TARGET "")
if(MINIAUDIO_CPP_ENGINE_TIMER_WHEELS_SOURCE_DIR)
    set(_tw_include "${MINIAUDIO_CPP_ENGINE_TIMER_WHEELS_SOURCE_DIR}/include")
    if(NOT EXISTS "${_tw_include}/timer_wheels/awaitable_timer.h")
        message(FATAL_ERROR
            "MINIAUDIO_CPP_ENGINE_TIMER_WHEELS_SOURCE_DIR does not contain timer_wheels headers")
    endif()

    add_library(miniaudio_cpp_engine_timer_wheels_headers INTERFACE)
    target_compile_features(miniaudio_cpp_engine_timer_wheels_headers INTERFACE cxx_std_20)
    target_include_directories(miniaudio_cpp_engine_timer_wheels_headers INTERFACE "${_tw_include}")
    set(MINIAUDIO_CPP_ENGINE_TIMER_WHEELS_TARGET miniaudio_cpp_engine_timer_wheels_headers)

    if(MINIAUDIO_CPP_ENGINE_VERIFY_TIMER_WHEELS_REVISION AND _tw_expected_revision)
        find_package(Git QUIET)
        if(NOT Git_FOUND)
            message(FATAL_ERROR
                "Git is required to verify MINIAUDIO_CPP_ENGINE_TIMER_WHEELS_REVISION; "
                "disable MINIAUDIO_CPP_ENGINE_VERIFY_TIMER_WHEELS_REVISION only for an independently verified tree")
        endif()
        execute_process(
            COMMAND ${GIT_EXECUTABLE} rev-parse HEAD
            WORKING_DIRECTORY "${MINIAUDIO_CPP_ENGINE_TIMER_WHEELS_SOURCE_DIR}"
            OUTPUT_VARIABLE _tw_revision
            OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE _tw_revision_result
        )
        if(NOT _tw_revision_result EQUAL 0)
            message(FATAL_ERROR
                "Could not verify the timer_wheels source revision at "
                "${MINIAUDIO_CPP_ENGINE_TIMER_WHEELS_SOURCE_DIR}")
        endif()
        message(STATUS "timer_wheels source revision: ${_tw_revision}")
        if(NOT _tw_revision STREQUAL _tw_expected_revision)
            message(FATAL_ERROR
                "timer_wheels is ${_tw_revision}; expected PR-stack revision "
                "${_tw_expected_revision}")
        endif()
    endif()
elseif(TARGET timer_wheels::timer_wheels)
    set(MINIAUDIO_CPP_ENGINE_TIMER_WHEELS_TARGET timer_wheels::timer_wheels)
else()
    find_package(timer_wheels CONFIG QUIET)
    if(TARGET timer_wheels::timer_wheels)
        set(MINIAUDIO_CPP_ENGINE_TIMER_WHEELS_TARGET timer_wheels::timer_wheels)
    endif()
endif()

if(MINIAUDIO_CPP_ENGINE_TIMER_WHEELS_TARGET)
    add_library(miniaudio_cpp_engine_timer_wheels INTERFACE)
    add_library(miniaudio::cpp_engine_timer_wheels ALIAS miniaudio_cpp_engine_timer_wheels)
    target_compile_features(miniaudio_cpp_engine_timer_wheels INTERFACE cxx_std_20)
    target_link_libraries(miniaudio_cpp_engine_timer_wheels INTERFACE
        miniaudio_cpp_engine
        ${MINIAUDIO_CPP_ENGINE_TIMER_WHEELS_TARGET}
    )
endif()

if(MINIAUDIO_BUILD_TESTS)
    add_test(
        NAME miniaudio_cpp_engine_steamaudio_arch
        COMMAND ${CMAKE_COMMAND}
            "-DMINIAUDIO_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/steamaudio_arch_tests.cmake"
    )

    add_executable(miniaudio_cpp_engine_tests tests/core_tests.cpp)
    target_compile_features(miniaudio_cpp_engine_tests PRIVATE cxx_std_20)
    set_target_properties(miniaudio_cpp_engine_tests PROPERTIES CXX_EXTENSIONS OFF)
    target_link_libraries(miniaudio_cpp_engine_tests PRIVATE miniaudio_cpp_engine)
    target_compile_options(miniaudio_cpp_engine_tests PRIVATE ${MINIAUDIO_CPP_ENGINE_COMPILE_OPTIONS})
    if(NOT MINIAUDIO_NO_NULL AND
       (NOT MINIAUDIO_ENABLE_ONLY_SPECIFIC_BACKENDS OR MINIAUDIO_ENABLE_NULL))
        target_compile_definitions(miniaudio_cpp_engine_tests PRIVATE MINIAUDIO_CPP_ENGINE_TEST_NULL_BACKEND=1)
    endif()
    add_test(NAME miniaudio_cpp_engine COMMAND miniaudio_cpp_engine_tests)
    set_tests_properties(miniaudio_cpp_engine PROPERTIES TIMEOUT 10)

    if(MINIAUDIO_CPP_ENGINE_BUILD_ALLOCATION_TESTS)
        add_executable(miniaudio_cpp_engine_allocation_tests tests/allocation_tests.cpp)
        target_compile_features(miniaudio_cpp_engine_allocation_tests PRIVATE cxx_std_20)
        set_target_properties(miniaudio_cpp_engine_allocation_tests PROPERTIES CXX_EXTENSIONS OFF)
        target_link_libraries(miniaudio_cpp_engine_allocation_tests PRIVATE miniaudio_cpp_engine)
        target_compile_options(miniaudio_cpp_engine_allocation_tests PRIVATE ${MINIAUDIO_CPP_ENGINE_COMPILE_OPTIONS})
        if(UNIX AND NOT APPLE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_definitions(miniaudio_cpp_engine_allocation_tests
                PRIVATE MINIAUDIO_CPP_ENGINE_WRAP_C_ALLOCATIONS=1)
            target_link_options(miniaudio_cpp_engine_allocation_tests PRIVATE
                -Wl,--wrap=malloc
                -Wl,--wrap=calloc
                -Wl,--wrap=realloc
                -Wl,--wrap=aligned_alloc
                -Wl,--wrap=posix_memalign
            )
        endif()
        add_test(NAME miniaudio_cpp_engine_allocations COMMAND miniaudio_cpp_engine_allocation_tests)
    endif()

    if(HAS_STEAMAUDIO)
        add_executable(miniaudio_cpp_engine_steamaudio_tests tests/steam_audio_tests.cpp)
        target_compile_features(miniaudio_cpp_engine_steamaudio_tests PRIVATE cxx_std_20)
        set_target_properties(miniaudio_cpp_engine_steamaudio_tests PROPERTIES CXX_EXTENSIONS OFF)
        target_link_libraries(miniaudio_cpp_engine_steamaudio_tests PRIVATE miniaudio_cpp_engine_steamaudio)
        target_compile_options(miniaudio_cpp_engine_steamaudio_tests PRIVATE ${MINIAUDIO_CPP_ENGINE_COMPILE_OPTIONS})
        if(UNIX AND NOT APPLE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_definitions(miniaudio_cpp_engine_steamaudio_tests
                PRIVATE MINIAUDIO_CPP_ENGINE_WRAP_C_ALLOCATIONS=1)
            target_link_options(miniaudio_cpp_engine_steamaudio_tests PRIVATE
                -Wl,--wrap=malloc
                -Wl,--wrap=calloc
                -Wl,--wrap=realloc
                -Wl,--wrap=aligned_alloc
                -Wl,--wrap=posix_memalign
            )
        endif()
        add_test(NAME miniaudio_cpp_engine_steamaudio COMMAND miniaudio_cpp_engine_steamaudio_tests)
    endif()

    if(MINIAUDIO_CPP_ENGINE_TIMER_WHEELS_TARGET)
        add_executable(miniaudio_cpp_engine_timer_wheels_tests tests/timer_wheels_tests.cpp)
        target_compile_features(miniaudio_cpp_engine_timer_wheels_tests PRIVATE cxx_std_20)
        set_target_properties(miniaudio_cpp_engine_timer_wheels_tests PROPERTIES CXX_EXTENSIONS OFF)
        target_link_libraries(miniaudio_cpp_engine_timer_wheels_tests PRIVATE miniaudio::cpp_engine_timer_wheels)
        target_compile_options(miniaudio_cpp_engine_timer_wheels_tests PRIVATE ${MINIAUDIO_CPP_ENGINE_COMPILE_OPTIONS})
        add_test(NAME miniaudio_cpp_engine_timer_wheels COMMAND miniaudio_cpp_engine_timer_wheels_tests)
    endif()
endif()

if(MINIAUDIO_CPP_ENGINE_BUILD_BENCHMARKS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" OR MSVC)
        message(FATAL_ERROR "The C++ engine cache benchmark requires a GNU-compatible compiler barrier.")
    endif()
    add_executable(miniaudio_cpp_engine_cache_benchmark benchmarks/cache_benchmark.cpp)
    target_compile_features(miniaudio_cpp_engine_cache_benchmark PRIVATE cxx_std_20)
    set_target_properties(miniaudio_cpp_engine_cache_benchmark PROPERTIES CXX_EXTENSIONS OFF)
    target_link_libraries(miniaudio_cpp_engine_cache_benchmark PRIVATE miniaudio_cpp_engine)
    target_compile_options(miniaudio_cpp_engine_cache_benchmark PRIVATE ${MINIAUDIO_CPP_ENGINE_COMPILE_OPTIONS})
endif()

if(MINIAUDIO_INSTALL)
    if(IS_ABSOLUTE "${CMAKE_INSTALL_INCLUDEDIR}")
        set(MINIAUDIO_CPP_ENGINE_PC_INCLUDEDIR "${CMAKE_INSTALL_INCLUDEDIR}")
    else()
        set(MINIAUDIO_CPP_ENGINE_PC_INCLUDEDIR "\${prefix}/${CMAKE_INSTALL_INCLUDEDIR}")
    endif()
    if(IS_ABSOLUTE "${CMAKE_INSTALL_LIBDIR}")
        set(MINIAUDIO_CPP_ENGINE_PC_LIBDIR "${CMAKE_INSTALL_LIBDIR}")
    else()
        set(MINIAUDIO_CPP_ENGINE_PC_LIBDIR "\${exec_prefix}/${CMAKE_INSTALL_LIBDIR}")
    endif()
    configure_file(miniaudio-cpp-engine.pc.in miniaudio-cpp-engine.pc @ONLY)

    if(MINIAUDIO_BUILD_TESTS AND UNIX)
        find_package(PkgConfig QUIET)
        if(PKG_CONFIG_FOUND AND NOT IS_ABSOLUTE "${CMAKE_INSTALL_INCLUDEDIR}" AND
           NOT IS_ABSOLUTE "${CMAKE_INSTALL_LIBDIR}")
            set(_pc_space_prefix "${CMAKE_CURRENT_BINARY_DIR}/install with spaces")
            set(_pc_space_dir "${CMAKE_CURRENT_BINARY_DIR}/pkgconfig-space")
            set(_pc_space_runtime_dir
                "${CMAKE_CURRENT_BINARY_DIR}/runtime libraries with spaces")
            file(MAKE_DIRECTORY "${_pc_space_dir}")
            file(MAKE_DIRECTORY "${_pc_space_runtime_dir}")
            set(_saved_install_prefix "${CMAKE_INSTALL_PREFIX}")
            set(_saved_pc_includedir "${MINIAUDIO_PC_INCLUDEDIR}")
            set(_saved_pc_libdir "${MINIAUDIO_PC_LIBDIR}")
            set(_saved_pc_libs_private "${MINIAUDIO_PC_LIBS_PRIVATE}")
            set(CMAKE_INSTALL_PREFIX "${_pc_space_prefix}")
            configure_file(miniaudio-cpp-engine.pc.in
                "${_pc_space_dir}/miniaudio-cpp-engine.pc" @ONLY)
            set(MINIAUDIO_PC_INCLUDEDIR "\${prefix}/${CMAKE_INSTALL_INCLUDEDIR}")
            set(MINIAUDIO_PC_LIBDIR "\${exec_prefix}/${CMAKE_INSTALL_LIBDIR}")

            set(_pc_space_private_input)
            set(_pc_space_expected_private)
            foreach(_pc_runtime_library IN LISTS COMMON_LINK_LIBRARIES)
                if(IS_ABSOLUTE "${_pc_runtime_library}")
                    get_filename_component(_pc_runtime_name "${_pc_runtime_library}" NAME)
                    set(_pc_runtime_link "${_pc_space_runtime_dir}/${_pc_runtime_name}")
                    file(REMOVE "${_pc_runtime_link}")
                    file(CREATE_LINK "${_pc_runtime_library}" "${_pc_runtime_link}"
                        SYMBOLIC RESULT _pc_link_result)
                    if(NOT _pc_link_result STREQUAL "0")
                        message(FATAL_ERROR
                            "Could not create pkg-config runtime-library fixture: ${_pc_link_result}")
                    endif()
                    list(APPEND _pc_space_private_input "${_pc_runtime_link}")
                    list(APPEND _pc_space_expected_private "${_pc_runtime_link}")
                else()
                    list(APPEND _pc_space_private_input "${_pc_runtime_library}")
                endif()
            endforeach()
            miniaudio_pkgconfig_link_flags(
                MINIAUDIO_PC_LIBS_PRIVATE ${_pc_space_private_input})
            string(JOIN "|" _pc_space_expected_private_encoded
                ${_pc_space_expected_private})
            configure_file("${PROJECT_SOURCE_DIR}/miniaudio.pc.in"
                "${_pc_space_dir}/miniaudio.pc" @ONLY)
            set(CMAKE_INSTALL_PREFIX "${_saved_install_prefix}")
            set(MINIAUDIO_PC_INCLUDEDIR "${_saved_pc_includedir}")
            set(MINIAUDIO_PC_LIBDIR "${_saved_pc_libdir}")
            set(MINIAUDIO_PC_LIBS_PRIVATE "${_saved_pc_libs_private}")
            add_test(
                NAME miniaudio_cpp_engine_pkgconfig_spaces
                COMMAND ${CMAKE_COMMAND}
                    "-DPKG_CONFIG_EXECUTABLE=${PKG_CONFIG_EXECUTABLE}"
                    "-DPC_DIR=${_pc_space_dir}"
                    "-DEXPECTED_CPP_INCLUDE=${_pc_space_prefix}/${CMAKE_INSTALL_INCLUDEDIR}/miniaudio"
                    "-DEXPECTED_C_INCLUDE=${_pc_space_prefix}/${CMAKE_INSTALL_INCLUDEDIR}"
                    "-DEXPECTED_LIBDIR=${_pc_space_prefix}/${CMAKE_INSTALL_LIBDIR}"
                    "-DEXPECTED_PRIVATE_LIBRARIES=${_pc_space_expected_private_encoded}"
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/pkgconfig_space_tests.cmake"
            )
        endif()
    endif()

    install(TARGETS miniaudio_cpp_engine
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
    if(HAS_STEAMAUDIO)
        install(TARGETS miniaudio_cpp_engine_steamaudio
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        )
    endif()
    install(DIRECTORY include/miniaudio_cpp_engine
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/miniaudio
        FILES_MATCHING PATTERN "*.hpp"
    )
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/miniaudio-cpp-engine.pc
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
endif()
