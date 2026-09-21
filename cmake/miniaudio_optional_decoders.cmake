include(FetchContent)

set(MINIAUDIO_PINNED_LIBOGG_VERSION "1.3.6")
set(MINIAUDIO_PINNED_LIBVORBIS_VERSION "1.3.7+1b75110b5a2754ba1931d82dd83cb822b266a21d")
set(MINIAUDIO_PINNED_LIBOPUS_VERSION "1.6.1")
set(MINIAUDIO_PINNED_OPUSFILE_VERSION "0.12")

function(_miniaudio_fetch_archive name url sha256)
    cmake_policy(PUSH)
    if(POLICY CMP0135)
        cmake_policy(SET CMP0135 NEW)
    endif()

    FetchContent_Declare(${name}
        URL "${url}"
        URL_HASH "SHA256=${sha256}"
    )
    FetchContent_GetProperties(${name})
    if(NOT ${name}_POPULATED)
        if(POLICY CMP0169)
            cmake_policy(SET CMP0169 OLD)
        endif()
        FetchContent_Populate(${name})
    endif()

    cmake_policy(POP)
    set(${name}_SOURCE_DIR "${${name}_SOURCE_DIR}" PARENT_SCOPE)
    set(${name}_BINARY_DIR "${${name}_BINARY_DIR}" PARENT_SCOPE)
endfunction()

function(_miniaudio_fetch_libogg)
    if(TARGET ogg OR TARGET Ogg::ogg)
        return()
    endif()

    _miniaudio_fetch_archive(miniaudio_libogg
        "https://downloads.xiph.org/releases/ogg/libogg-1.3.6.tar.xz"
        "5c8253428e181840cd20d41f3ca16557a9cc04bad4a3d04cce84808677fa1061"
    )
    add_subdirectory("${miniaudio_libogg_SOURCE_DIR}" "${miniaudio_libogg_BINARY_DIR}" EXCLUDE_FROM_ALL)
endfunction()

function(_miniaudio_ogg_target output_variable)
    if(TARGET Ogg::ogg)
        set(${output_variable} Ogg::ogg PARENT_SCOPE)
    elseif(TARGET ogg)
        set(${output_variable} ogg PARENT_SCOPE)
    else()
        message(FATAL_ERROR "The pinned optional decoder build requires a libogg target.")
    endif()
endfunction()

function(_miniaudio_fetch_libvorbis)
    if(TARGET vorbisfile)
        return()
    endif()

    _miniaudio_fetch_libogg()
    _miniaudio_ogg_target(_ogg_target)
    _miniaudio_fetch_archive(miniaudio_libvorbis_source
        "https://github.com/xiph/vorbis/archive/1b75110b5a2754ba1931d82dd83cb822b266a21d.tar.gz"
        "bd0f08c8a63debdce27dd71de7d953f9c92e410d1cfe263220bcb49fff2ab749"
    )

    set(_vorbis_sources
        mdct.c
        smallft.c
        block.c
        envelope.c
        window.c
        lsp.c
        lpc.c
        analysis.c
        synthesis.c
        psy.c
        info.c
        floor1.c
        floor0.c
        res0.c
        mapping0.c
        registry.c
        codebook.c
        sharedbook.c
        lookup.c
        bitrate.c
    )
    list(TRANSFORM _vorbis_sources PREPEND "${miniaudio_libvorbis_source_SOURCE_DIR}/lib/")
    if(WIN32)
        list(APPEND _vorbis_sources
            "${miniaudio_libvorbis_source_SOURCE_DIR}/lib/vorbisenc.c"
            "${miniaudio_libvorbis_source_SOURCE_DIR}/win32/vorbis.def"
        )
    endif()

    add_library(vorbis ${_vorbis_sources})
    target_include_directories(vorbis
        PUBLIC "${miniaudio_libvorbis_source_SOURCE_DIR}/include"
        PRIVATE "${miniaudio_libvorbis_source_SOURCE_DIR}/lib"
    )
    target_link_libraries(vorbis PUBLIC ${_ogg_target})
    if(UNIX)
        target_link_libraries(vorbis PRIVATE m)
    endif()
    if(MSVC)
        target_compile_definitions(vorbis PRIVATE
            _CRT_SECURE_NO_WARNINGS
            _CRT_SECURE_NO_DEPRECATE
            _CRT_NONSTDC_NO_DEPRECATE
        )
    endif()

    add_library(vorbisfile "${miniaudio_libvorbis_source_SOURCE_DIR}/lib/vorbisfile.c")
    target_include_directories(vorbisfile PUBLIC "${miniaudio_libvorbis_source_SOURCE_DIR}/include")
    target_link_libraries(vorbisfile PUBLIC vorbis)
    if(MSVC)
        target_sources(vorbisfile PRIVATE "${miniaudio_libvorbis_source_SOURCE_DIR}/win32/vorbisfile.def")
        target_compile_definitions(vorbisfile PRIVATE
            _CRT_SECURE_NO_WARNINGS
            _CRT_SECURE_NO_DEPRECATE
            _CRT_NONSTDC_NO_DEPRECATE
        )
    endif()
endfunction()

function(_miniaudio_fetch_libopus)
    if(TARGET opus OR TARGET Opus::opus)
        return()
    endif()

    _miniaudio_fetch_archive(miniaudio_libopus_source
        "https://downloads.xiph.org/releases/opus/opus-1.6.1.tar.gz"
        "6ffcb593207be92584df15b32466ed64bbec99109f007c82205f0194572411a1"
    )
    set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
    set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
    add_subdirectory("${miniaudio_libopus_source_SOURCE_DIR}" "${miniaudio_libopus_source_BINARY_DIR}" EXCLUDE_FROM_ALL)
endfunction()

function(_miniaudio_opus_target output_variable)
    if(TARGET Opus::opus)
        set(${output_variable} Opus::opus PARENT_SCOPE)
    elseif(TARGET opus)
        set(${output_variable} opus PARENT_SCOPE)
    else()
        message(FATAL_ERROR "The pinned optional decoder build requires a libopus target.")
    endif()
endfunction()

function(_miniaudio_fetch_opusfile)
    if(TARGET opusfile)
        return()
    endif()

    _miniaudio_fetch_libogg()
    _miniaudio_fetch_libopus()
    _miniaudio_ogg_target(_ogg_target)
    _miniaudio_opus_target(_opus_target)
    _miniaudio_fetch_archive(miniaudio_opusfile_source
        "https://downloads.xiph.org/releases/opus/opusfile-0.12.tar.gz"
        "118d8601c12dd6a44f52423e68ca9083cc9f2bfe72da7a8c1acb22a80ae3550b"
    )

    add_library(opusfile
        "${miniaudio_opusfile_source_SOURCE_DIR}/src/info.c"
        "${miniaudio_opusfile_source_SOURCE_DIR}/src/internal.c"
        "${miniaudio_opusfile_source_SOURCE_DIR}/src/opusfile.c"
        "${miniaudio_opusfile_source_SOURCE_DIR}/src/stream.c"
    )
    add_library(OpusFile::opusfile ALIAS opusfile)
    target_include_directories(opusfile PUBLIC "${miniaudio_opusfile_source_SOURCE_DIR}/include")
    target_link_libraries(opusfile PUBLIC ${_ogg_target} ${_opus_target})
    if(UNIX)
        target_link_libraries(opusfile PRIVATE m)
        target_compile_definitions(opusfile PRIVATE OP_HAVE_LRINTF)
    endif()
    if(MSVC)
        target_compile_options(opusfile PRIVATE /wd4090 /wd4244 /wd4267)
    elseif(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(opusfile PRIVATE -Wno-long-long -Wno-parentheses)
    endif()
endfunction()

function(miniaudio_fetch_optional_decoder_dependencies)
    if(MINIAUDIO_FETCH_LIBVORBIS)
        message(STATUS
            "Fetching pinned optional Vorbis dependencies: libogg ${MINIAUDIO_PINNED_LIBOGG_VERSION}, "
            "libvorbis ${MINIAUDIO_PINNED_LIBVORBIS_VERSION}")
        _miniaudio_fetch_libvorbis()
    endif()

    if(MINIAUDIO_FETCH_LIBOPUS)
        message(STATUS
            "Fetching pinned optional Opus dependencies: libogg ${MINIAUDIO_PINNED_LIBOGG_VERSION}, "
            "libopus ${MINIAUDIO_PINNED_LIBOPUS_VERSION}, opusfile ${MINIAUDIO_PINNED_OPUSFILE_VERSION}")
        _miniaudio_fetch_opusfile()
    endif()
endfunction()
