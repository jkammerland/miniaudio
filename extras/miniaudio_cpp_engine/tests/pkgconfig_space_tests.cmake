foreach(required IN ITEMS
        PKG_CONFIG_EXECUTABLE
        PC_DIR
        EXPECTED_CPP_INCLUDE
        EXPECTED_C_INCLUDE
        EXPECTED_LIBDIR
        EXPECTED_PRIVATE_LIBRARIES)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "PKG_CONFIG_PATH=${PC_DIR}"
        "PKG_CONFIG_LIBDIR=${PC_DIR}"
        "${PKG_CONFIG_EXECUTABLE}" --cflags --libs --static miniaudio-cpp-engine
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "pkg-config failed (${result}): ${error}")
endif()

separate_arguments(arguments UNIX_COMMAND "${output}")
foreach(expected IN ITEMS
        "-I${EXPECTED_CPP_INCLUDE}"
        "-I${EXPECTED_C_INCLUDE}"
        "-L${EXPECTED_LIBDIR}")
    list(FIND arguments "${expected}" index)
    if(index EQUAL -1)
        message(FATAL_ERROR "missing one-token argument '${expected}' in: ${output}")
    endif()
endforeach()

if(EXPECTED_PRIVATE_LIBRARIES)
    string(REPLACE "|" ";" expected_private_libraries "${EXPECTED_PRIVATE_LIBRARIES}")
    foreach(expected IN LISTS expected_private_libraries)
        list(FIND arguments "${expected}" index)
        if(index EQUAL -1)
            message(FATAL_ERROR
                "runtime library was not preserved as one token '${expected}' in: ${output}")
        endif()
    endforeach()
endif()
