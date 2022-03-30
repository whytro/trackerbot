set(SpdLog_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/spdlog/include/spdlog)

find_package(PkgConfig)
pkg_check_modules(PC_SpdLog QUIET SpdLog)

find_path(SpdLog_INCLUDE_DIR
    PATHS ${PC_SpdLog_INCLUDE_DIRS}
    PATH_SUFFIXES spdlog
)

set(SpdLog_VERSION ${PC_SpdLog_VERSION})
mark_as_advanced(SpdLog_FOUND SpdLog_INCLUDE_DIR SpdLog_VERSION)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(SpdLog
    REQUIRED_VARS SpdLog_INCLUDE_DIR
    VERSION_VAR SpdLog_VERSION
)

if(SpdLog_FOUND)
    get_filename_component(SpdLog_INCLUDE_DIRS ${SpdLog_INCLUDE_DIR} DIRECTORY)
endif()

if(SpdLog_FOUND AND NOT TARGET SpdLog::SpdLog)
    add_library(SpdLog::SpdLog INTERFACE IMPORTED)
    set_target_properties(SpdLog::SpdLog PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${SpdLog_INCLUDE_DIRS}"
    )
endif()
