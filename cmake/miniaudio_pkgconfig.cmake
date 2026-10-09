function(miniaudio_pkgconfig_link_flags output_variable)
    set(result)
    foreach(link_item IN LISTS ARGN)
        if(IS_ABSOLUTE "${link_item}")
            # pkg-config parses Libs fields as a shell-like argument list.
            # Preserve an absolute library filename as one token even when its
            # directory contains spaces.
            string(REPLACE "\\" "\\\\" escaped_link_item "${link_item}")
            string(REPLACE "\"" "\\\"" escaped_link_item "${escaped_link_item}")
            list(APPEND result "\"${escaped_link_item}\"")
        elseif(link_item MATCHES "^-")
            list(APPEND result "${link_item}")
        else()
            list(APPEND result "-l${link_item}")
        endif()
    endforeach()
    string(JOIN " " result_string ${result})
    set(${output_variable} "${result_string}" PARENT_SCOPE)
endfunction()
