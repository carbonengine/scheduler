cmake_minimum_required(VERSION 3.31)

project(SchedulerCapiTest)

find_package(Python3 COMPONENTS Development Interpreter REQUIRED)
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

target_link_libraries(SchedulerCapiTest PRIVATE GTest::gtest GTest::gtest_main Python3::Python CcpCore)

# Generate paths Include for each config
set(SCHEDULER_CEXTENSION_MODULE_PATH $<TARGET_FILE_DIR:Scheduler>)
set(SCHEDULER_PACKAGE_PATH "${CMAKE_SOURCE_DIR}/python")
set(STDLIB_PATH ${Python3_STDLIB})
set(GREENLET_CEXTENSION_MODULE_PATH ${GREENLET_IMPORTED_LOCATION_DIR})
set(GREENLET_MODULE_PATH ${GREENLET_PYTHON_MODULE_DIR})

if (WIN32)
    add_custom_command(
            TARGET SchedulerCapiTest POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
            $<TARGET_RUNTIME_DLLS:SchedulerCapiTest>
            $<TARGET_FILE_DIR:SchedulerCapiTest>
            COMMAND_EXPAND_LISTS
            VERBATIM
    )
endif()

gtest_discover_tests(
        SchedulerCapiTest capiTests
        DISCOVERY_MODE PRE_TEST
        PROPERTIES
        ENVIRONMENT "BUILDFLAVOR=$<LOWER_CASE:$<CONFIG>>"
        ENVIRONMENT "SCHEDULER_CEXTENSION_MODULE_PATH=${SCHEDULER_CEXTENSION_MODULE_PATH}"
        ENVIRONMENT "SCHEDULER_PACKAGE_PATH=${SCHEDULER_PACKAGE_PATH}"
        ENVIRONMENT "STDLIB_PATH=${STDLIB_PATH}"
        ENVIRONMENT "GREENLET_CEXTENSION_MODULE_PATH=${GREENLET_CEXTENSION_MODULE_PATH}"
        ENVIRONMENT "GREENLET_MODULE_PATH=${GREENLET_MODULE_PATH}"
)
