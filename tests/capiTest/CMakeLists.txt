cmake_minimum_required(VERSION 3.18)

project(SchedulerCapiTest)

find_package(Python REQUIRED NO_CMAKE_PATH)
find_package(GTest REQUIRED NO_CMAKE_PATH)

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

target_include_directories(SchedulerCapiTest PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)

target_include_directories(SchedulerCapiTest PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

target_precompile_headers(SchedulerCapiTest PRIVATE StdAfx.h)

target_link_libraries(SchedulerCapiTest PRIVATE GTest::GTest GTest::Main Python Scheduler)

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
std::wstring STDLIB_PATH = L\"${BRANCH_ROOT_DIR}/carbon/common/stdlib/\";
std::wstring GREENLET_CEXTENSION_MODULE_PATH = L\"${Greenlet_ROOT}/${CCP_VENDOR_BIN_PATH}\";
std::wstring GREENLET_MODULE_PATH = L\"${Greenlet_ROOT}/python\";
"
)

# Copy the python dll to target build directory
if(WIN32)
    set(PythonLibName "python312.dll")
elseif(APPLE)
    set(PythonLibName "libpython3.12.dylib")
endif()

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

add_custom_command(
  TARGET SchedulerCapiTest POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy 
    ${Python_ROOT}/${CCP_VENDOR_BIN_PATH}/${PythonLibName}
    $<TARGET_FILE_DIR:SchedulerCapiTest>)

gtest_discover_tests(SchedulerCapiTest capiTests
    DISCOVERY_MODE PRE_TEST
    PROPERTIES
        ENVIRONMENT "BUILDFLAVOR=$<LOWER_CASE:$<CONFIG>>"
)
