project(NGemity)
cmake_minimum_required(VERSION 3.10)

########## CONAN ##########
if(NOT EXISTS "${CMAKE_BINARY_DIR}/conan.cmake")
   message(STATUS "Downloading conan.cmake from https://github.com/conan-io/cmake-conan")
   file(DOWNLOAD "https://github.com/conan-io/cmake-conan/raw/v0.14/conan.cmake"
                 "${CMAKE_BINARY_DIR}/conan.cmake")
endif()

include(${CMAKE_BINARY_DIR}/conan.cmake)
conan_check(VERSION 1.0.0 REQUIRED)

conan_add_remote(NAME bincrafters URL https://api.bintray.com/conan/bincrafters/public-conan) # Add bincrafters repo
conan_add_remote(NAME conan URL https://api.bintray.com/conan/conan-community/conan) # add (default?) conan repo

conan_cmake_run(CONANFILE conanfile.txt BASIC_SETUP BUILD missing) # run conan setup
conan_basic_setup(TARGETS)
######## END CONAN ########

# set macro-directory
set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/macros")

if(CMAKE_COMPILER_IS_GNUCXX AND NOT MINGW)
    add_definitions(-fno-delete-null-pointer-checks)
endif()

# build in Release-mode by default if not explicitly set
if( NOT CMAKE_BUILD_TYPE )
    set(CMAKE_BUILD_TYPE "Release")
endif()
if(CMAKE_BUILD_TYPE MATCHES "Debug")
    set(DEBUG 1)
endif()

option(WITHOUT_GIT          "Disable the GIT testing routines"               0)
option(WITH_PCH             "Use Precompiled Headers"                        1)
option(WITH_TOOLS           "Also compile additional tools"                  1)
option(NG_USE_CLITHREAD     "Use CLI Thread (readline on Linux)"             0)

find_package(Platform REQUIRED)
find_package(PCHSupport REQUIRED)
IF(NG_USE_CLITHREAD)
    find_package(Readline REQUIRED)
    add_definitions(-DNG_USE_CLITHREAD)
endif()

#Include platform/compiler-specific definitions
include(${CMAKE_SOURCE_DIR}/cmake/SetDefinitions.cmake)

IF(NOT WITHOUT_GIT)
    find_package(Git)
ENDIF()

if(WITH_PCH)
    message("Compiling with PrecompiledHeaders")
else()
    message("Compiling without PrecompiledHeaders")
endif()

# Find revision ID and hash of the sourcetree
include(cmake/genrev.cmake)
add_custom_target(revision.h ALL
        COMMAND ${CMAKE_COMMAND} -DNO_GIT=${WITHOUT_GIT} -DGIT_EXEC=${GIT_EXECUTABLE} -DBUILDDIR=${CMAKE_BINARY_DIR} -P ${CMAKE_SOURCE_DIR}/cmake/genrev.cmake
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_subdirectory(dep/liblua)
add_subdirectory(shared)
add_subdirectory(Mononoke)
add_subdirectory(Chihiro)
if(WITH_TOOLS)
    add_subdirectory(Tools)
endif()