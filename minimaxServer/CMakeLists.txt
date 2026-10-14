cmake_minimum_required(VERSION 3.21)
project(minimax_server_tools LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# C++: mmsign (Ed25519 manifest signer) -------------------------------------------------
include(FetchContent)
FetchContent_Declare(libsodium_cmake GIT_REPOSITORY https://github.com/robinlinden/libsodium-cmake.git GIT_TAG master)
set(SODIUM_DISABLE_TESTS ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(libsodium_cmake)
add_executable(mmsign native/mmsign.cpp)
target_link_libraries(mmsign PRIVATE sodium)
add_custom_command(TARGET mmsign POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/bin
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:mmsign> ${CMAKE_SOURCE_DIR}/bin/
  VERBATIM)

# C#: MmUpdateTool (needs the .NET 8 SDK; skipped when `dotnet` is missing) --------------
find_program(DOTNET dotnet)
if(DOTNET)
  add_custom_target(mmupdatetool ALL
    COMMAND ${DOTNET} publish ${CMAKE_SOURCE_DIR}/csharp/MmUpdateTool.csproj -c Release -o ${CMAKE_SOURCE_DIR}/bin/MmUpdateTool
    COMMENT "Publishing MmUpdateTool (C#)")
else()
  message(STATUS "dotnet not found - MmUpdateTool (C#) skipped")
endif()

# Python tests --------------------------------------------------------------------------
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  enable_testing()
  add_test(NAME server COMMAND ${Python3_EXECUTABLE} -m unittest discover -s tests
           WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()
