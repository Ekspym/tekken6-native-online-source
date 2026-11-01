# Include at the END of the project's root CMakeLists.txt.
option(TEKKEN6_EXPAND_MENU_ROWS "Build 18-row native menu storage" ON)
if(TEKKEN6_EXPAND_MENU_ROWS)
    if(NOT TARGET tekken6_recomp)
        message(STATUS "Generate guest code, then configure again to adapt the native practice menu")
        return()
    endif()
    set(TEKKEN6_ROW_PYTHON "" CACHE FILEPATH "Python interpreter for menu source adaptation")
    if(NOT TEKKEN6_ROW_PYTHON)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        set(TEKKEN6_ROW_PYTHON "${Python3_EXECUTABLE}")
    endif()
    get_target_property(_t6_row_sources tekken6_recomp SOURCES)
    set(_t6_row_new_sources "")
    foreach(_t6_row_source IN LISTS _t6_row_sources)
        get_filename_component(_t6_row_name "${_t6_row_source}" NAME)
        if(_t6_row_name MATCHES "^tekken6_recomp\\.[0-9]+\\.cpp$")
            get_filename_component(_t6_row_input "${_t6_row_source}" ABSOLUTE
                BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
            set(_t6_row_output "${CMAKE_CURRENT_BINARY_DIR}/expanded-menu-rows/${_t6_row_name}")
            add_custom_command(OUTPUT "${_t6_row_output}"
                COMMAND "${TEKKEN6_ROW_PYTHON}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/expand_menu_rows.py"
                    --input "${_t6_row_input}" --output "${_t6_row_output}"
                DEPENDS "${_t6_row_input}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/expand_menu_rows.py"
                VERBATIM)
            get_source_file_property(_t6_row_options "${_t6_row_source}" COMPILE_OPTIONS)
            if(_t6_row_options)
                set_source_files_properties("${_t6_row_output}" PROPERTIES
                    COMPILE_OPTIONS "${_t6_row_options}")
            endif()
            list(APPEND _t6_row_new_sources "${_t6_row_output}")
        else()
            list(APPEND _t6_row_new_sources "${_t6_row_source}")
        endif()
    endforeach()
    add_custom_target(tekken6_validate_expanded_menu_rows
        COMMAND "${TEKKEN6_ROW_PYTHON}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/expand_menu_rows.py"
            --validate-dir "${CMAKE_CURRENT_SOURCE_DIR}/generated/default"
        DEPENDS ${_t6_row_sources}
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/expand_menu_rows.py"
        VERBATIM)
    add_dependencies(tekken6_validate_expanded_menu_rows tekken6_codegen)
    add_dependencies(tekken6_recomp tekken6_validate_expanded_menu_rows)
    set_property(TARGET tekken6_recomp PROPERTY SOURCES "${_t6_row_new_sources}")
    target_include_directories(tekken6_recomp PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/practice")
endif()
