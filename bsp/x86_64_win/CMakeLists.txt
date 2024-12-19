# CMake minimum version
cmake_minimum_required(VERSION 3.5.0)
set(CMAKE_BUILD_TYPE Debug)

# Project str and language
project(mr-library LANGUAGES C ASM)

# C standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED True)

# Include directories
include_directories("${PROJECT_NAME}/include")

# Source files
file(GLOB_RECURSE HEADERS "${PROJECT_NAME}/include/*.h")
file(GLOB_RECURSE SOURCES "${PROJECT_NAME}/source/*.c")
file(GLOB_RECURSE PORT_SOURCES "${PROJECT_NAME}/port/*.c")
file(GLOB_RECURSE ASM_SCRIPTS "${PROJECT_NAME}/port/*.S")
file(GLOB_RECURSE LINK_SCRIPTS "*.lds")

# Add executable
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-Map,mapfile -m64")
add_executable(${PROJECT_NAME} ${HEADERS} ${SOURCES} ${PORT_SOURCES} ${ASM_SCRIPTS} main.c)

# Link libraries
target_link_libraries(${PROJECT_NAME})
set_target_properties(${PROJECT_NAME} PROPERTIES LINK_FLAGS "-T${LINK_SCRIPTS}" LINKER_LANGUAGE C)
