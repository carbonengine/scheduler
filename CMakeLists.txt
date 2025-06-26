cmake_minimum_required(VERSION 3.31)
project(Scheduler)

include(cmake/CcpGlobalSettings.cmake)
include(cmake/CcpVendorUtilities.cmake)
include(cmake/CcpTargetConfigurations.cmake)
include(cmake/CcpDocsGenerator.cmake)
include(cmake/CcpPackageConfigHelpers.cmake)

find_package(Python3 COMPONENTS Development Interpreter REQUIRED)
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
ccp_add_library(Scheduler MODULE ${SRC_FILES})

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
    INTERNAL_POSTFIX ""
    TRINITYDEV_POSTFIX ""
)

# Provide an install target iff this is the top level project only
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    get_target_property(GREENLET_IMPORTED_LOCATION Greenlet IMPORTED_LOCATION)
    get_filename_component(GREENLET_IMPORTED_LOCATION_DIR ${GREENLET_IMPORTED_LOCATION} DIRECTORY)

    # Store python path in environment for subsequent build steps
    if(WIN32)
        SET(ENV{PYTHONPATH} "${CMAKE_SOURCE_DIR}/python\;$<TARGET_FILE_DIR:Scheduler>\;${GREENLET_IMPORTED_LOCATION_DIR}/python\;${GREENLET_IMPORTED_LOCATION_DIR}")
    elseif(APPLE)
        SET(ENV{PYTHONPATH} "${CMAKE_SOURCE_DIR}/python:$<TARGET_FILE_DIR:Scheduler>:${GREENLET_IMPORTED_LOCATION_DIR}/python:${GREENLET_IMPORTED_LOCATION_DIR}")
    endif()

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
        # Run sphinx
        set(SPHINX_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/doc/source)
        set(SPHINX_BUILD ${CMAKE_CURRENT_BINARY_DIR}/doc/build)

        create_carbon_docs_sphinx_target(
                PYTHON_EXE ${Python3_EXECUTABLE}
                VENV_NAME docs_generation_venv
                PYTHONPATH_ENV $ENV{PYTHONPATH}
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

        execute_process(COMMAND ${Python3_EXECUTABLE} discover.py
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

        separate_arguments(PYTHON_TESTS UNIX_COMMAND ${PYTHON_TESTS_STR})

        foreach (PYTHON_TEST ${PYTHON_TESTS})
            message(STATUS "Found test ${PYTHON_TEST}")
            add_test(NAME ${PYTHON_TEST}
                COMMAND ${Python3_EXECUTABLE} -m unittest -v ${PYTHON_TEST}
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/tests/python/scheduler/tests
            )
            set_tests_properties(${PYTHON_TEST} PROPERTIES ENVIRONMENT "PYTHONPATH=$ENV{PYTHONPATH};BUILDFLAVOR=$<LOWER_CASE:$<CONFIG>>")
        endforeach ()

        set(ALL_PYTHON_TEST RunAllPythonTestsInSingleInterpreterSession)
        add_test(NAME ${ALL_PYTHON_TEST}
            COMMAND ${Python3_EXECUTABLE} -m unittest discover -v
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests/python/scheduler/tests
            COMMAND_EXPAND_LISTS
        )
        set_tests_properties(${ALL_PYTHON_TEST} PROPERTIES ENVIRONMENT "PYTHONPATH=$ENV{PYTHONPATH};BUILDFLAVOR=$<LOWER_CASE:$<CONFIG>>")
    endif()

    option(INSTALL_TO_MONOLITH "Install this package to a perforce branch vendor folder - provided for backwards compatability" ON)

    if (INSTALL_TO_MONOLITH)
        install(
                TARGETS Scheduler
                EXPORT SchedulerTargets
                CONFIGURATIONS Release
                LIBRARY DESTINATION lib/${CCP_PLATFORM}/${CCP_ARCHITECTURE}/${CCP_TOOLSET}/
                ARCHIVE DESTINATION lib/${CCP_PLATFORM}/${CCP_ARCHITECTURE}/${CCP_TOOLSET}/
                RUNTIME DESTINATION bin/${CCP_PLATFORM}/${CCP_ARCHITECTURE}/${CCP_TOOLSET}/
        )
        # Install rule for available public headers
        install(FILES ${PUBLIC_HEADER_FILES} DESTINATION include)

        configure_ccp_vendor_config_file(
                TARGET Scheduler
                DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/SchedulerConfig.cmake"
                CONFIGURATIONS Release
        )
        install(FILES ${CMAKE_CURRENT_BINARY_DIR}/SchedulerConfig.cmake DESTINATION .)
        # Install scheduler extension package
        install(DIRECTORY python/scheduler DESTINATION python
                FILES_MATCHING
                PATTERN "*.py"
        )
    else()
        install(
                TARGETS Scheduler
                EXPORT SchedulerTargets
                CONFIGURATIONS Release
                LIBRARY DESTINATION lib/
        )
        # Install rule to ensure that our runtime and linker files are in the expected, platform-specific folders
        install(DIRECTORY include DESTINATION . FILES_MATCHING PATTERN "*.h")
        install(DIRECTORY python/scheduler DESTINATION bin/python FILES_MATCHING PATTERN "*.py")

        install(
                EXPORT SchedulerTargets
                DESTINATION ${CMAKE_INSTALL_PREFIX}/share/carbon-scheduler
                FILE carbon-schedulerConfig.cmake
        )
    endif()
endif()
