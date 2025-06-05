add_library(Scheduler INTERFACE IMPORTED)

set(Scheduler_INCLUDE_DIR "${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include")
set(Scheduler_Libraries _scheduler)

if(APPLE)
    set(_SHARED_LIBRARY_SUFFIX ".so")
else()
    set(_SHARED_LIBRARY_SUFFIX ${CMAKE_SHARED_LIBRARY_SUFFIX})
endif()

if(APPLE)
    set_target_properties(Scheduler PROPERTIES
        IMPORTED_LOCATION "${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib/_scheduler.so"
    )
elseif(WIN32)
    set_target_properties(Scheduler PROPERTIES
        IMPORTED_LOCATION "${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/_scheduler.pyd"
    )
else()
    message(FATAL_ERROR "carbon-scheduler not supported on platform.")
endif()

set_target_properties(Scheduler PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${Scheduler_INCLUDE_DIR}"
    INTERFACE_LINK_LIBRARIES ${Scheduler_Libraries}
)
