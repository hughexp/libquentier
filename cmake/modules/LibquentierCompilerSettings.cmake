if(CMAKE_COMPILER_IS_GNUCXX)
  execute_process(COMMAND ${CMAKE_C_COMPILER} -dumpversion OUTPUT_VARIABLE GCC_VERSION)
  message(STATUS "Using GNU C++ compiler, version ${GCC_VERSION}")
  if(GCC_VERSION VERSION_GREATER 4.8 OR GCC_VERSION VERSION_EQUAL 4.8)
    message(STATUS "This version of GNU C++ compiler supports C++11 standard.")
    set(CMAKE_CXX_FLAGS "-std=gnu++11 ${CMAKE_CXX_FLAGS}")
    add_definitions("-DCPP11_COMPLIANT=1")
  elseif(GCC_VERSION VERSION_LESS 4.4)
    message(FATAL_ERROR "This version of GNU C++ compiler is known to not support C++11 standard as much as it is required 
                         to build this application. Consider upgrading the compiler to version 4.4 at least")
  else()
    set(CMAKE_CXX_FLAGS "-std=c++0x ${CMAKE_CXX_FLAGS}")
  endif()
  set(CMAKE_CXX_FLAGS "-Wno-uninitialized -fvisibility=hidden ${CMAKE_CXX_FLAGS}")
  if("${CMAKE_SYSTEM_NAME}" STREQUAL "Linux")
    set(CMAKE_CXX_FLAGS "-fPIC -rdynamic ${CMAKE_CXX_FLAGS}")
  endif()
  if(NOT "${CMAKE_SYSTEM_NAME}" STREQUAL "Windows")
    set(CMAKE_CXX_FLAGS "-ldl ${CMAKE_CXX_FLAGS}")
  else()
    add_definitions("-DUNICODE -D_UNICODE")
  endif()
elseif("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang") # NOTE: MATCHES is required, STREQUAL leads to problems with AppleClang
  execute_process(COMMAND ${CMAKE_C_COMPILER} --version OUTPUT_VARIABLE CLANG_VERSION)
  message(STATUS "Using LLVM/Clang C++ compiler, version info: ${CLANG_VERSION}")
  if(NOT ${CMAKE_CXX_COMPILER_VERSION} VERSION_LESS 3.1)
    message(STATUS "Your compiler supports C++11 standard.")
    add_definitions("-DCPP11_COMPLIANT=1")
  else()
    message(WARNING "Your compiler may not support all the necessary C++11 standard features
                     to build this library. If you get any compilation errors, consider
                     upgrading to a compiler version which fully supports the C++11 standard.")
  endif()
  set(CMAKE_CXX_FLAGS "-std=c++11 ${CMAKE_CXX_FLAGS}")
  set(CMAKE_CXX_FLAGS "-Wno-uninitialized -Wno-null-conversion -Wno-format -Wno-deprecated ${CMAKE_CXX_FLAGS}")
  if(NOT "${CMAKE_SYSTEM_NAME}" STREQUAL "Windows")
    set(CMAKE_CXX_FLAGS "-fPIC ${CMAKE_CXX_FLAGS}")
  endif()
  # set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS_INIT} $ENV{LDFLAGS} -fsanitize=undefined")
  if("${CMAKE_SYSTEM_NAME}" STREQUAL "Darwin" OR USE_LIBCPP)
    find_library(LIBCPP NAMES libc++.so libc++.so.1.0 libc++.dylib OPTIONAL)
    if(LIBCPP)
      message(STATUS "Using native Clang's C++ standard library: ${LIBCPP}")
      set(CMAKE_CXX_FLAGS "-stdlib=libc++ ${CMAKE_CXX_FLAGS}")
      add_definitions("-DHAVELIBCPP")
    endif()
  endif()
elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
  if(MSVC_VERSION GREATER_EQUAL 1800)
    message(STATUS "This version of Visual C++ compiler supports C++11 standard.")
    add_definitions("-DCPP11_COMPLIANT")
  elseif(MSVC_VERSION GREATER_EQUAL 1600)
    message(STATUS "This version of Visual C++ compiler only partially supports
                    C++11 standard but it is capable of building libquentier")
  else()
    message(STATUS "This version of Visual C++ compiler might not be standard
                    compliant enough to build libquentier.
                    If you get any compilation errors, consider upgrading to
                    a compiler version which fully supports C++11 standard.")
  endif()
  set(CMAKE_CXX_FLAGS "-D_SCL_SECURE_NO_WARNINGS -D_CRT_SECURE_NO_WARNINGS ${CMAKE_CXX_FLAGS}")
  add_definitions("-DUNICODE -D_UNICODE")
else()
  message(WARNING "Your C++ compiler is not officially supported for building of this library.
                   If you get any compilation errors, consider upgrading to a compiler version
                   which fully supports C++11 standard.")
endif(CMAKE_COMPILER_IS_GNUCXX)
