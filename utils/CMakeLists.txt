if(BUILD_SHARED_LIBS)
  set(EXTRA_LIBS ${EXTRA_LIBS} wampcc_shared wampcc_json_shared Threads::Threads)
else()
  set(EXTRA_LIBS ${EXTRA_LIBS} wampcc_static wampcc_json_static Threads::Threads)
endif()

# Threads needed because std::condition_variable::wait_for() on libstdc++
# uses pthreads.

if(BUILD_UTILS)

  add_executable(admin "${PROJECT_SOURCE_DIR}/utils/admin.cc")
  target_link_libraries (admin ${EXTRA_LIBS})

  # Platforms without getopt.h (e.g. MSVC) use the bundled implementation
  if(NOT HAVE_GETOPT_H)
    target_sources(admin PRIVATE "${PROJECT_SOURCE_DIR}/3rdparty/getopt/getopt_long.c")
    target_include_directories(admin PRIVATE
      "${PROJECT_SOURCE_DIR}/3rdparty/getopt"
      "${PROJECT_SOURCE_DIR}/3rdparty/getopt/compat")
    # upstream code calls getenv(), which MSVC flags as deprecated (C4996)
    set_source_files_properties("${PROJECT_SOURCE_DIR}/3rdparty/getopt/getopt_long.c"
      PROPERTIES COMPILE_DEFINITIONS _CRT_SECURE_NO_WARNINGS)
  endif()

  ##
  ## Install targets
  ##
  install (TARGETS admin DESTINATION "${INSTALL_BIN_DIR}" COMPONENT util)

  set_property(GLOBAL APPEND PROPERTY ${PROJECT_NAME}_TARGETS admin)
endif() # BUILD_UTILS
