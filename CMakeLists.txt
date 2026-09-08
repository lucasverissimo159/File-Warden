cmake_minimum_required(VERSION 3.16)
project(filewarden VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options()

find_package(Threads REQUIRED)

add_library(filewarden_core
    src/sha256.cpp
    src/file_scanner.cpp
    src/duplicate_finder.cpp
    src/manifest.cpp
    src/backup_engine.cpp
    src/huffman.cpp
    src/cli.cpp
)
target_include_directories(filewarden_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(filewarden_core PUBLIC Threads::Threads)

# GCC before version 9 ships <filesystem> as a separate library that needs
# explicit linking. Harmless to skip on newer toolchains where it isn't needed.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.0)
  target_link_libraries(filewarden_core PUBLIC stdc++fs)
endif()

add_executable(filewarden src/main.cpp)
target_link_libraries(filewarden PRIVATE filewarden_core)

option(FILEWARDEN_BUILD_TESTS "Build the FileWarden test suite" ON)
if(FILEWARDEN_BUILD_TESTS)
  enable_testing()
  add_executable(filewarden_tests
      tests/test_main.cpp
      tests/test_sha256.cpp
      tests/test_duplicate_finder.cpp
      tests/test_backup_engine.cpp
      tests/test_manifest.cpp
      tests/test_cli.cpp
      tests/test_huffman.cpp
  )
  target_include_directories(filewarden_tests PRIVATE tests)
  target_link_libraries(filewarden_tests PRIVATE filewarden_core)
  add_test(NAME filewarden_tests COMMAND filewarden_tests)
endif()
