if(BUILD_SHARED_LIBS)
  set(EXTRA_LIBS ${EXTRA_LIBS} wampcc_shared wampcc_json_shared Threads::Threads)
else()
  set(EXTRA_LIBS ${EXTRA_LIBS} wampcc_static wampcc_json_static Threads::Threads)
endif()

# Threads needed because std::condition_variable::wait_for() on libstdc++
# uses pthreads.

if(BUILD_UTILS)

  if (MSVC)
    add_executable(admin
      "${PROJECT_SOURCE_DIR}/utils/admin.cc"
      "${PROJECT_SOURCE_DIR}/utils/getopt.c")
    target_include_directories(admin
      PRIVATE "${PROJECT_SOURCE_DIR}/utils")
  else()
    add_executable(admin "${PROJECT_SOURCE_DIR}/utils/admin.cc")
  endif()

  target_link_libraries (admin ${EXTRA_LIBS})

  ##
  ## Install targets
  ##
  install (TARGETS admin DESTINATION "${INSTALL_BIN_DIR}" COMPONENT util)

  set_property(GLOBAL APPEND PROPERTY ${PROJECT_NAME}_TARGETS admin)
endif() # BUILD_UTILS
