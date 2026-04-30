cmake_minimum_required(VERSION 3.16)
project(CocoStackPass)

# Find LLVM
find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

# LLVM is normally built without RTTI. Be consistent with that.
if(LLVM_ENABLE_RTTI)
  message(STATUS "LLVM was built with RTTI. Building Pass with RTTI.")
else()
  message(STATUS "LLVM was built without RTTI. Building Pass without RTTI.")
  if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /GR-")
  else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti")
  endif()
endif()

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add pass library
add_library(CocoStackPass MODULE
  CocoStackPass.cpp
)

target_include_directories(CocoStackPass PRIVATE
  ${LLVM_INCLUDE_DIRS}
)

target_compile_definitions(CocoStackPass PRIVATE
  __STDC_CONSTANT_MACROS
  __STDC_FORMAT_MACROS
  __STDC_LIMIT_MACROS
)

target_link_libraries(CocoStackPass PRIVATE
  LLVMCore
  LLVMSupport
  LLVMCodeGen
  LLVMTarget
)

# Set output directory
set_target_properties(CocoStackPass PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

# On macOS, use .dylib extension
if(APPLE)
  set_target_properties(CocoStackPass PROPERTIES
    SUFFIX ".dylib"
  )
endif()

# Install target (optional)
install(TARGETS CocoStackPass
  LIBRARY DESTINATION lib
)