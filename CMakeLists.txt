cmake_minimum_required(VERSION 3.16)
project(Scheduler)



if(NOT DEFINED ENV{CCP_EVE_PERFORCE_BRANCH_PATH})
    message(FATAL_ERROR "Missing required environment variable CCP_EVE_PERFORCE_BRANCH_PATH")
elseif(NOT IS_ABSOLUTE $ENV{CCP_EVE_PERFORCE_BRANCH_PATH})
    message(FATAL_ERROR "CCP_EVE_PERFORCE_BRANCH_PATH needs to be an absolute path")
elseif(NOT EXISTS $ENV{CCP_EVE_PERFORCE_BRANCH_PATH})
    message(FATAL_ERROR "CCP_EVE_PERFORCE_BRANCH_PATH is pointing at a non-existing location $ENV{CCP_EVE_PERFORCE_BRANCH_PATH}")
elseif(NOT IS_DIRECTORY $ENV{CCP_EVE_PERFORCE_BRANCH_PATH})
    message(FATAL_ERROR "CCP_EVE_PERFORCE_BRANCH_PATH needs to be a directory")
else()
    message(STATUS "Using CCP_EVE_PERFORCE_BRANCH_PATH located at $ENV{CCP_EVE_PERFORCE_BRANCH_PATH}")
endif()

file(TO_CMAKE_PATH "$ENV{CCP_EVE_PERFORCE_BRANCH_PATH}" BRANCH_ROOT_DIR)
list(INSERT CMAKE_MODULE_PATH 0 ${BRANCH_ROOT_DIR}/cmake)
include(CcpGlobalSettings)
include(CcpPackageConfigHelpers)

find_package(CcpCore REQUIRED NO_CMAKE_PATH)
find_package(Python REQUIRED NO_CMAKE_PATH)
find_package(Greenlet REQUIRED NO_CMAKE_PATH)

get_target_property(GREENLET_INCLUDE_DIRECTORY
        Greenlet
        INTERFACE_INCLUDE_DIRECTORIES)

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

target_include_directories(Scheduler PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)

target_link_libraries(Scheduler PRIVATE Python)
target_link_libraries(Scheduler PUBLIC CcpCore)

target_include_directories(Scheduler
        PRIVATE
        ${GREENLET_INCLUDE_DIRECTORY}
        )

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
        find_package(Doxygen)

        # Evaluate config file for Doxygen to input project values
        set(DOXYFILE_IN ${CMAKE_CURRENT_SOURCE_DIR}/doc/Doxyfile.in)
        set(DOXYFILE_OUT ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)
        set(DOXYGEN_INDEX_FILE ${CMAKE_CURRENT_BINARY_DIR}/xml/index.xml)
        configure_file(${DOXYFILE_IN} ${DOXYFILE_OUT} @ONLY)

        # Regenerate with source changes
        add_custom_command(OUTPUT ${DOXYGEN_INDEX_FILE}
                            DEPENDS ${SRC_FILES}
                            COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYFILE_OUT}
                            MAIN_DEPENDENCY ${DOXYFILE_OUT} ${DOXYFILE_IN}
                            COMMENT "Running Doxygen"
                            VERBATIM)

        add_custom_target(Doxygen ALL DEPENDS ${DOXYGEN_INDEX_FILE})

        # Set up sphinx build
        set(SPHINX_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/doc/source)
        set(SPHINX_BUILD ${CMAKE_CURRENT_BINARY_DIR}/doc/build)

        # Run sphinx
        if(WIN32)
            set(PYTHON_PATH "${Python_ROOT}/${CCP_VENDOR_BIN_PATH}\;${BRANCH_ROOT_DIR}/packages\;${BRANCH_ROOT_DIR}/carbon/common/stdlib\;${BRANCH_ROOT_DIR}/carbon/common/lib\;$<TARGET_FILE_DIR:Scheduler>\;${CMAKE_CURRENT_SOURCE_DIR}/python\;${Greenlet_ROOT}/${CCP_VENDOR_BIN_PATH}\;${Greenlet_ROOT}/python")
        elseif(APPLE)
            set(PYTHON_PATH "${Python_ROOT}/${CCP_VENDOR_BIN_PATH}:${BRANCH_ROOT_DIR}/packages:${BRANCH_ROOT_DIR}/carbon/common/stdlib:${BRANCH_ROOT_DIR}/carbon/common/lib:$<TARGET_FILE_DIR:Scheduler>:${CMAKE_CURRENT_SOURCE_DIR}/python:${Greenlet_ROOT}/${CCP_VENDOR_BIN_PATH}:${Greenlet_ROOT}/python")
        endif()

        add_custom_target(Sphinx ALL
                COMMAND ${CMAKE_COMMAND} -E env PYTHONPATH=${PYTHON_PATH} ${PYTHON_EXECUTABLE} -m sphinx build -E -b html -D breathe_projects.doxygen=${CMAKE_CURRENT_BINARY_DIR}/docs/xml -c ${SPHINX_SOURCE} ${SPHINX_SOURCE} ${SPHINX_BUILD}
                WORKING_DIRECTORY ${BRANCH_ROOT_DIR}
                DEPENDS ${DOXYGEN_INDEX_FILE}
                COMMENT "Generating documentation with Sphinx")

        # Ensure that Scheduler is built before Spinx target
        add_dependencies(Sphinx Scheduler)

        # Install rule for documentation
        install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/doc/build/ DESTINATION documentation)
    endif()

    enable_testing()

    # Build test
    add_subdirectory(tests/capiTest)

    # Install rule to ensure that our runtime and linker files are in the expected, platform-specific folders
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

    if(WIN32)
        set(ENV{PYTHONPATH} ${CMAKE_SOURCE_DIR}/python/\;${BRANCH_ROOT_DIR}/carbon/common/stdlib\;${Python_ROOT}/${CCP_VENDOR_BIN_PATH})
    elseif(APPLE)
        set(ENV{PYTHONPATH} ${CMAKE_SOURCE_DIR}/python/:${BRANCH_ROOT_DIR}/carbon/common/stdlib:${Python_ROOT}/${CCP_VENDOR_BIN_PATH})
    endif()

    execute_process(COMMAND ${PYTHON_EXECUTABLE} discover.py
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

    get_filename_component(PYTHON_TEST_EXE_DIR ${PYTHON_EXECUTABLE} DIRECTORY)

    if(WIN32)
        SET(PYTHONPATH_ENV "${CMAKE_SOURCE_DIR}/python/\;$<TARGET_FILE_DIR:Scheduler>\;${BRANCH_ROOT_DIR}/carbon/common/stdlib/\;${PYTHON_TEST_EXE_DIR}\;${Greenlet_ROOT}/${CCP_VENDOR_BIN_PATH}\;${Greenlet_ROOT}/python")
    elseif(APPLE)
        SET(PYTHONPATH_ENV "${CMAKE_SOURCE_DIR}/python/:$<TARGET_FILE_DIR:Scheduler>:${BRANCH_ROOT_DIR}/carbon/common/stdlib/:${PYTHON_TEST_EXE_DIR}:${Greenlet_ROOT}/${CCP_VENDOR_BIN_PATH}:${Greenlet_ROOT}/python")
    endif()

    separate_arguments(PYTHON_TESTS UNIX_COMMAND ${PYTHON_TESTS_STR})

    foreach (PYTHON_TEST ${PYTHON_TESTS})
        message(STATUS "Found test ${PYTHON_TEST}")
        add_test(NAME ${PYTHON_TEST}
            COMMAND ${PYTHON_EXECUTABLE} -m unittest -v ${PYTHON_TEST}
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/tests/python/scheduler/tests
        )
        set_tests_properties(${PYTHON_TEST} PROPERTIES ENVIRONMENT "PYTHONPATH=${PYTHONPATH_ENV};BUILDFLAVOR=$<LOWER_CASE:$<CONFIG>>")
    endforeach ()

    set(ALL_PYTHON_TEST RunAllPythonTestsInSingleInterpreterSession)
    add_test(NAME ${ALL_PYTHON_TEST}
        COMMAND ${PYTHON_EXECUTABLE} -m unittest discover -v
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests/python/scheduler/tests
        COMMAND_EXPAND_LISTS
    )
    set_tests_properties(${ALL_PYTHON_TEST} PROPERTIES ENVIRONMENT "PYTHONPATH=${PYTHONPATH_ENV};BUILDFLAVOR=$<LOWER_CASE:$<CONFIG>>")

    # Install scheduler extension package
    install(DIRECTORY python/scheduler DESTINATION python
            FILES_MATCHING
            PATTERN "*.py"
    )
endif()
