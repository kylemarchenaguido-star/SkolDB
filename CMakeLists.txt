cmake_minimum_required(VERSION 3.21)
project(SkolDB LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra -Wshadow)
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_compile_options(-g -fsanitize=address,undefined)
  add_link_options(-fsanitize=address,undefined)
endif()

add_library(skoldb STATIC
  db/src/crc32.cpp
  db/src/status.cpp
  db/src/file.cpp 
  db/src/io/fault_file.cpp 
)
target_include_directories(skoldb PUBLIC db/include)

option(TESTING "Build tests" ${PROJECT_IS_TOP_LEVEL})

if(TESTING)
  enable_testing()

  foreach(name coding_test crc32_test)
    add_executable(${name} db/tests/${name}.cpp)
    target_link_libraries(${name} PRIVATE skoldb)
    add_test(NAME ${name} COMMAND ${name})
  endforeach()
endif()






