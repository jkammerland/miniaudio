if(NOT DEFINED MINIAUDIO_SOURCE_DIR)
    message(FATAL_ERROR "MINIAUDIO_SOURCE_DIR is required")
endif()

include("${MINIAUDIO_SOURCE_DIR}/cmake/miniaudio_steamaudio_arch.cmake")

function(expect_arch system_name system_processor target_processor pointer_size expected)
    miniaudio_select_steamaudio_arch(
        actual "${system_name}" "${system_processor}" "${target_processor}" "${pointer_size}")
    if(NOT actual STREQUAL expected)
        message(FATAL_ERROR
            "${system_name}/${system_processor}/${target_processor}/${pointer_size}: "
            "expected '${expected}', got '${actual}'")
    endif()
endfunction()

expect_arch("Darwin"  "arm64"   "arm64"   8 "osx")
expect_arch("Linux"   "x86_64"  "x86_64"  8 "linux-x64")
expect_arch("Linux"   "i686"    "i686"    4 "linux-x86")
expect_arch("Windows" "AMD64"   "x64"     8 "windows-x64")
expect_arch("Windows" "AMD64"   "Win32"   4 "windows-x86")

# Steam Audio does not package these targets in the desktop SDK directories.
expect_arch("FreeBSD" "x86_64"  "x86_64"  8 "")
expect_arch("Android" "x86_64"  "x86_64"  8 "")
expect_arch("iOS"     "arm64"   "arm64"   8 "")
expect_arch("Windows" "ARM64"   "ARM64"   8 "")
