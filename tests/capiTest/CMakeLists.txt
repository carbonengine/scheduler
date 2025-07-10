cmake_minimum_required(VERSION 3.31)

project(SchedulerCapiTest)

find_package(Python3 COMPONENTS Development REQUIRED)
find_package(GTest CONFIG REQUIRED)

include(GoogleTest)

set(SRC_FILES
    InterpreterWithSchedulerModule.cpp
    InterpreterWithSchedulerModule.h
    Scheduler.cpp
    Channel.cpp
    Tasklet.cpp
    StdAfx.h
    StdAfx.cpp
)

add_executable(SchedulerCapiTest ${SRC_FILES})

get_target_property(SCHEDULER_INCLUDE_DIR Scheduler INTERFACE_INCLUDE_DIRECTORIES)

target_include_directories(SchedulerCapiTest
        PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}
        ${SCHEDULER_INCLUDE_DIR}
)

target_precompile_headers(SchedulerCapiTest PRIVATE StdAfx.h)

target_link_libraries(SchedulerCapiTest PRIVATE GTest::gtest GTest::gtest_main Python3::Python)

# Generate paths Include for each config
# Allows for easy use inside IDE
file (GENERATE
    OUTPUT "PackagePaths_$<CONFIG>.h"
    CONTENT
"
#pragma once
#include <string>
std::wstring SCHEDULER_CEXTENSION_MODULE_PATH = L\"$<TARGET_FILE_DIR:Scheduler>\";
std::wstring SCHEDULER_PACKAGE_PATH = L\"${CMAKE_SOURCE_DIR}/python\";
std::wstring STDLIB_PATH = L\"${Python3_STDLIB}\";
std::wstring GREENLET_CEXTENSION_MODULE_PATH = L\"${GREENLET_IMPORTED_LOCATION_DIR}\";
std::wstring GREENLET_MODULE_PATH = L\"${GREENLET_IMPORTED_LOCATION_DIR}/python\";
"
)

cmake_policy(SET CMP0175 OLD) # Allows us to use OUTPUT with PRE_BUILD in add_custom_command
add_custom_command (
        COMMAND ${CMAKE_COMMAND} "-E" "copy_if_different" "PackagePaths_$<CONFIG>.h" "PackagePaths.h"
        VERBATIM
        PRE_BUILD
        DEPENDS  "PackagePaths_$<CONFIG>.h"
        OUTPUT   "PackagePaths.h"
        COMMENT  "Creating PackagePaths.h file"
)

add_custom_target(GeneratePackagePathsHeader DEPENDS "PackagePaths.h")
add_dependencies(SchedulerCapiTest GeneratePackagePathsHeader)


if (APPLE)
    add_custom_command(
            TARGET SchedulerCapiTest POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
            $<TARGET_FILE:Python3::Python>
            $<TARGET_FILE_DIR:SchedulerCapiTest>
    )

    gtest_discover_tests(
            SchedulerCapiTest capiTests
            DISCOVERY_MODE PRE_TEST
            PROPERTIES
                ENVIRONMENT "BUILDFLAVOR=$<LOWER_CASE:$<CONFIG>>"
    )

elseif (WIN32)
    add_custom_command(
            TARGET SchedulerCapiTest POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
            $<TARGET_RUNTIME_DLLS:SchedulerCapiTest>
            $<TARGET_FILE_DIR:SchedulerCapiTest>
            COMMAND_EXPAND_LISTS
            VERBATIM
    )

    gtest_discover_tests(
            SchedulerCapiTest capiTests
            DISCOVERY_MODE PRE_TEST
            PROPERTIES
                ENVIRONMENT "BUILDFLAVOR=$<LOWER_CASE:$<CONFIG>>"
    )
endif ()
