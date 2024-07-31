# Once done this will define
#  jansson_FOUND        - System has jansson
#  jansson_INCLUDE_DIRS - The jansson include directories
#  jansson_LIBRARIES    - The libraries needed to use jansson

find_package(jansson NO_MODULE)
if (jansson_FOUND)
  set_target_properties(jansson::jansson PROPERTIES IMPORTED_GLOBAL TRUE)
  add_library(jansson ALIAS jansson::jansson)

  get_target_property(_location jansson::jansson LOCATION)
  set(jansson_LIBRARY ${_location})
  unset(_location)

  get_target_property(_includes jansson::jansson INTERFACE_INCLUDE_DIRECTORIES)
  set(jansson_INCLUDE_DIR ${_includes})
  unset(_includes)
else()
find_package(PkgConfig QUIET)
pkg_check_modules(PC_jansson QUIET jansson)

find_path(jansson_INCLUDE_DIR
  NAMES jansson.h
  HINTS ${PC_jansson_INCLUDE_DIRS} ${jansson_DIR}/include)

find_library(jansson_LIBRARY
  NAMES jansson_d jansson
  HINTS ${PC_jansson_LIBRARY_DIRS} ${jansson_DIR}/lib ${jansson_DIR}/lib/Debug)

if(jansson_INCLUDE_DIR)
  set(_version_regex "^#define[ \t]+JANSSON_VERSION[ \t]+\"([^\"]+)\".*")
  file(STRINGS "${JANSSON_INCLUDE_DIR}/jansson.h"
    jansson_VERSION REGEX "${_version_regex}")
  string(REGEX REPLACE "${_version_regex}" "\\1"
    jansson_VERSION "${jansson_VERSION}")
  unset(_version_regex)
endif()

include(FindPackageHandleStandardArgs)
# handle the QUIETLY and REQUIRED arguments and set jansson_FOUND to TRUE
# if all listed variables are TRUE and the requested version matches.
find_package_handle_standard_args(jansson REQUIRED_VARS
  jansson_LIBRARY jansson_INCLUDE_DIR
  VERSION_VAR jansson_VERSION)

if(jansson_FOUND)
  add_library(jansson UNKNOWN IMPORTED)
  set_target_properties(jansson PROPERTIES
	IMPORTED_LINK_INTERFACE_LANGUAGES "CXX"
	IMPORTED_LOCATION "${jansson_LIBRARY}"
	INTERFACE_INCLUDE_DIRECTORIES "${jansson_INCLUDE_DIR}")
endif()

mark_as_advanced(jansson_INCLUDE_DIR jansson_LIBRARY)
endif()
