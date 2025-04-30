cmake_minimum_required(VERSION 3.31)
project(Scheduler)

include(cmake/CcpGlobalSettings.cmake)
include(cmake/CcpVendorUtilities.cmake)
include(cmake/CcpTargetConfigurations.cmake)
include(cmake/CcpDocsGenerator.cmake)

find_package(Python3 COMPONENTS Development REQUIRED)
find_package(greenlet CONFIG REQUIRED)


set(SRC_FILES
    include/Scheduler.h
    src/SchedulerModule.cpp
    src/PyTasklet.h
    src/PyTasklet.cpp
    src/Tasklet.cpp
    src/Tasklet.h
    src/PyChannel.cpp
    src/PyChannel.h
    src/Channel.cpp
    src/Channel.h
    src/PythonCppType.cpp
    src/PythonCppType.h
    src/PyScheduleManager.cpp
    src/PyScheduleManager.h
    src/PyCallableWrapper.h
    src/ScheduleManager.cpp
    src/ScheduleManager.h
    src/stdafx.cpp
    src/GILRAII.cpp
    src/GILRAII.h
    src/Utils.h
    src/Utils.cpp
)

set(PUBLIC_HEADER_FILES include/Scheduler.h)
ccp_add_library(Scheduler SHARED ${SRC_FILES})

target_include_directories(Scheduler
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)

target_link_libraries(Scheduler PRIVATE Python3::Python)

get_target_property(GREENLET_INCLUDE_DIR Greenlet INTERFACE_INCLUDE_DIRECTORIES)

target_include_directories(Scheduler PRIVATE ${GREENLET_INCLUDE_DIR})

get_target_property(_SOURCES Scheduler SOURCES)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}"
        PREFIX "Sources"
        FILES ${_SOURCES}
)

target_precompile_headers(Scheduler PRIVATE src/stdafx.h)

set_target_properties(Scheduler PROPERTIES
    OUTPUT_NAME "_scheduler"
    DEBUG_POSTFIX ""
)

# Provide an install target iff this is the top level project only
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)

    if (WIN32)
        find_program(BUILT_PYTHON_EXECUTABLE python REQUIRED NO_SYSTEM_ENVIRONMENT_PATH NO_CMAKE_SYSTEM_PATH)
    elseif(APPLE)
        find_program(BUILT_PYTHON_EXECUTABLE python3 REQUIRED NO_SYSTEM_ENVIRONMENT_PATH NO_CMAKE_SYSTEM_PATH)
    endif()

    get_target_property(GREENLET_IMPORTED_LOCATION Greenlet IMPORTED_LOCATION)
    get_filename_component(PYTHON_TOOL_ROOT_DIR ${BUILT_PYTHON_EXECUTABLE} DIRECTORY)
    get_filename_component(GREENLET_IMPORTED_LOCATION_DIR ${GREENLET_IMPORTED_LOCATION} DIRECTORY)

    # Determine if/when to generate/build documentation. The rules are as follows:
    # - If running locally - Build documentation is OFF by default
    # - If running on a build agent (TeamCity) - Build documentation is ON by default
    # - If -DBUILD_DOCUMENTATION=ON/OFF flag is explicitly set, use it.
    set(BUILD_DOCUMENTATION_DEFAULT_FLAG OFF)
    if (DEFINED ENV{TEAMCITY_VERSION})
        set(BUILD_DOCUMENTATION_DEFAULT_FLAG ON)
    endif()
    message(STATUS "Document generation settings: default=${BUILD_DOCUMENTATION_DEFAULT_FLAG}, option BUILD_DOCUMENTATION=${BUILD_DOCUMENTATION}")
    option(BUILD_DOCUMENTATION "Build Documentation" ${BUILD_DOCUMENTATION_DEFAULT_FLAG})

    if(BUILD_DOCUMENTATION)

#        # Run sphinx
        if(WIN32)
            set(PYTHONPATH_ENV_SPHINX "${CMAKE_SOURCE_DIR}/python/\;$<TARGET_FILE_DIR:Scheduler>\;${PYTHON_TOOL_ROOT_DIR}/Lib\;${PYTHON_TOOL_ROOT_DIR}\;${GREENLET_IMPORTED_LOCATION_DIR}/python\;${GREENLET_IMPORTED_LOCATION_DIR}")
        elseif(APPLE)
            set(PYTHONPATH_ENV_SPHINX "${CMAKE_SOURCE_DIR}/python/:$<TARGET_FILE_DIR:Scheduler>:${PYTHON_TOOL_ROOT_DIR}:${GREENLET_IMPORTED_LOCATION_DIR}/python:${GREENLET_IMPORTED_LOCATION_DIR}")
        endif()
#
        set(SPHINX_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/doc/source)
        set(SPHINX_BUILD ${CMAKE_CURRENT_BINARY_DIR}/doc/build)

        create_carbon_docs_sphinx_target(
                PYTHON_EXE ${BUILT_PYTHON_EXECUTABLE}
                VENV_NAME docs_generation_venv
                PYTHONPATH_ENV ${PYTHONPATH_ENV_SPHINX}
                SPHINX_SOURCE ${SPHINX_SOURCE}
                SPHINX_BUILD ${SPHINX_BUILD}
                DOXYGEN_SRC_FILES ${SRC_FILES}
                SPHINX_TARGET_NAME Sphinx
                DOXYGEN_TARGET_NAME Doxygen
                INSTALL_DESTINATION documentation
        )

        # Ensure that Scheduler is built before Spinx target
        add_dependencies(Sphinx Scheduler)

    endif()

    option(BUILD_TESTING "Build and run tests" ON)
    if(BUILD_TESTING)
        enable_testing()

        # Build test
        add_subdirectory(tests/capiTest)

        if(WIN32)
            set(ENV{PYTHONPATH} ${CMAKE_SOURCE_DIR}/python/\;${PYTHON_TOOL_ROOT_DIR}/Lib)
        elseif(APPLE)
            set(ENV{PYTHONPATH} ${CMAKE_SOURCE_DIR}/python/:${PYTHON_TOOL_ROOT_DIR}/Lib)
        endif()

        execute_process(COMMAND ${BUILT_PYTHON_EXECUTABLE} discover.py
                OUTPUT_VARIABLE PYTHON_TESTS_STR
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_STRIP_TRAILING_WHITESPACE
                RESULT_VARIABLE PYTHON_TEST_DISCOVERY_RETVAL
                ERROR_VARIABLE PYTHON_TEST_DISCOVERY_ERRORS
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests/python/scheduler
        )

        if(${PYTHON_TEST_DISCOVERY_RETVAL})
            message(FATAL_ERROR "Error encountered during test discovery:\n=== stderr: ===\n${PYTHON_TEST_DISCOVERY_ERRORS}\n=== stdout: ===\n${PYTHON_TESTS_STR}")
        endif()

        if(WIN32)
            SET(PYTHONPATH_ENV "${CMAKE_SOURCE_DIR}/python/\;$<TARGET_FILE_DIR:Scheduler>\;${PYTHON_TOOL_ROOT_DIR}/Lib\;${PYTHON_TOOL_ROOT_DIR}\;${GREENLET_IMPORTED_LOCATION_DIR}/python\;${GREENLET_IMPORTED_LOCATION_DIR}")
        elseif(APPLE)
            SET(PYTHONPATH_ENV "${CMAKE_SOURCE_DIR}/python/:$<TARGET_FILE_DIR:Scheduler>:${PYTHON_TOOL_ROOT_DIR}/Lib:${PYTHON_TOOL_ROOT_DIR}:${GREENLET_IMPORTED_LOCATION_DIR}/python:${GREENLET_IMPORTED_LOCATION_DIR}")
        endif()

        separate_arguments(PYTHON_TESTS UNIX_COMMAND ${PYTHON_TESTS_STR})

        foreach (PYTHON_TEST ${PYTHON_TESTS})
            message(STATUS "Found test ${PYTHON_TEST}")
            add_test(NAME ${PYTHON_TEST}
                COMMAND ${BUILT_PYTHON_EXECUTABLE} -m unittest -v ${PYTHON_TEST}
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/tests/python/scheduler/tests
            )
            set_tests_properties(${PYTHON_TEST} PROPERTIES ENVIRONMENT "PYTHONPATH=${PYTHONPATH_ENV};BUILDFLAVOR=$<LOWER_CASE:$<CONFIG>>")
        endforeach ()

        set(ALL_PYTHON_TEST RunAllPythonTestsInSingleInterpreterSession)
        add_test(NAME ${ALL_PYTHON_TEST}
            COMMAND ${BUILT_PYTHON_EXECUTABLE} -m unittest discover -v
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests/python/scheduler/tests
            COMMAND_EXPAND_LISTS
        )
        set_tests_properties(${ALL_PYTHON_TEST} PROPERTIES ENVIRONMENT "PYTHONPATH=${PYTHONPATH_ENV};BUILDFLAVOR=$<LOWER_CASE:$<CONFIG>>")
    endif()

    set(INSTALL_BIN_ONLY OFF CACHE BOOL "Do not Install configuration files meant to be read by projects using this project as a dependency (Cmake Config/ header files etc . . .). Set this to ON when installing this component to the perforce vendor folder")
    if(INSTALL_BIN_ONLY)
        # Install rule to ensure that our runtime and linker files are in the expected, platform-specific folders
        install(
                TARGETS Scheduler
                EXPORT SchedulerTargets
                CONFIGURATIONS Release
                RUNTIME DESTINATION bin/
        )
        # Install scheduler extension package
        install(DIRECTORY python/scheduler DESTINATION bin/python/
                FILES_MATCHING
                PATTERN "*.py"
        )
        install(FILES ${GREENLET_IMPORTED_LOCATION} DESTINATION bin/)
    else()
        # Install rule to ensure that our runtime and linker files are in the expected, platform-specific folders
        install(DIRECTORY include DESTINATION . FILES_MATCHING PATTERN "*.h")
        install(FILES python/scheduler/__init__.py DESTINATION bin/python/scheduler/)

        install(
                TARGETS Scheduler
                EXPORT SchedulerTargets
                CONFIGURATIONS ${CMAKE_CONFIGURATION_TYPES}
                ARCHIVE DESTINATION lib/
                LIBRARY DESTINATION lib/
                RUNTIME DESTINATION bin/
        )
        install(
                EXPORT SchedulerTargets
                DESTINATION ${CMAKE_INSTALL_PREFIX}/share/carbon-scheduler  # Calling this carbon-pdm
                FILE carbon-schedulerConfig.cmake
        )
    endif()
endif()
