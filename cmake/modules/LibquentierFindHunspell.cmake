if(NOT HUNSPELL_INCLUDE_DIR AND NOT HUNSPELL_LIBRARIES)
  find_path(HUNSPELL_INCLUDE_DIR NAMES hunspell/hunspell.hxx PATHS ${CMAKE_INCLUDE_PATH})
  if(NOT HUNSPELL_INCLUDE_DIR)
    message(FATAL_ERROR "Can't find development headers for hunspell library")
  else()
    include_directories(${HUNSPELL_INCLUDE_DIR})
  endif()

  find_library(HUNSPELL_LIBRARIES
               NAMES
               hunspell hunspell-1.7 hunspell-1.6 hunspell-1.5 hunspell-1.4 hunspell-1.3 hunspell-1.2
               libhunspell libhunspell-1.7 libhunspell-1.6 libhunspell-1.5 libhunspell-1.4 libhunspell-1.3 libhunspell-1.2)
  if(NOT HUNSPELL_LIBRARIES)
    message(FATAL_ERROR "Can't find hunspell library")
  endif()
endif()

message(STATUS "Found hunspell library: ${HUNSPELL_LIBRARIES}")

try_compile(HUNSPELL_NEW_API_AVAILABLE "${CMAKE_BINARY_DIR}/hunspell_api_check"
            "${CMAKE_SOURCE_DIR}/cmake/modules/hunspell_new_api_check.cpp"
            LINK_LIBRARIES "${HUNSPELL_LIBRARIES}"
            CMAKE_FLAGS
              "-DINCLUDE_DIRECTORIES=\"${HUNSPELL_INCLUDE_DIR}\"")

if (HUNSPELL_NEW_API_AVAILABLE)
  message(STATUS "Using std::string based libhunspell API")
  add_definitions("-DHUNSPELL_NEW_API_AVAILABLE")
endif()

get_filename_component(HUNSPELL_LIB_DIR "${HUNSPELL_LIBRARIES}" PATH)
