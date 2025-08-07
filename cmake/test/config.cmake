# SPDX-FileCopyrightText: 2006-2025 Knut Reinert & Freie Universität Berlin
# SPDX-FileCopyrightText: 2016-2025 Knut Reinert & MPI für molekulare Genetik
# SPDX-License-Identifier: CC0-1.0

list (APPEND CMAKE_CTEST_ARGUMENTS "--output-on-failure") # Must be before `enable_testing ()`.
list (APPEND CMAKE_CTEST_ARGUMENTS "--no-tests=error") # Must be before `enable_testing ()`.
CPMGetPackage (googletest)

enable_testing ()

# Set directories for test output files, input data and binaries.
file (MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/output)
add_definitions (-DOUTPUTDIR=\"${CMAKE_CURRENT_BINARY_DIR}/output/\")
add_definitions (-DDATADIR=\"${CMAKE_CURRENT_BINARY_DIR}/data/\")
add_definitions (-DBINDIR=\"${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/\")

# Add the test interface library.
if (NOT TARGET shopping_cart_queue_test)
    add_library (shopping_cart_queue_test INTERFACE)
    target_link_libraries (shopping_cart_queue_test INTERFACE GTest::gtest_main shopping_cart_queue_lib)
    add_library (shopping_cart_queue::test ALIAS shopping_cart_queue_test)
endif ()

# Add the check target that builds and runs tests.
add_custom_target (check COMMAND ${CMAKE_CTEST_COMMAND} ${CMAKE_CTEST_ARGUMENTS})

get_directory_property (shopping_cart_queue_targets DIRECTORY "${shopping_cart_queue_SOURCE_DIR}/src" BUILDSYSTEM_TARGETS)
foreach (target IN LISTS shopping_cart_queue_targets)
    get_target_property (type ${target} TYPE)
    if (type STREQUAL "EXECUTABLE")
        list (APPEND shopping_cart_queue_EXECUTABLE_LIST ${target})
    endif ()
endforeach ()
unset (shopping_cart_queue_targets)

macro (add_app_test test_filename)
    file (RELATIVE_PATH source_file "${shopping_cart_queue_SOURCE_DIR}" "${CMAKE_CURRENT_LIST_DIR}/${test_filename}")
    get_filename_component (target "${source_file}" NAME_WE)

    add_executable (${target} ${test_filename})
    target_link_libraries (${target} shopping_cart_queue::test)

    if (shopping_cart_queue_EXECUTABLE_LIST)
        add_dependencies (${target} ${shopping_cart_queue_EXECUTABLE_LIST})
    endif ()
    add_dependencies (check ${target})

    add_test (NAME ${target} COMMAND ${target})

    unset (source_file)
    unset (target)
endmacro ()
