cmake_minimum_required(VERSION 3.14)
project(ParityGameSolver CXX C)

include(ExternalProject)

# Add your dependency
ExternalProject_Add(
    OxiDD
    GIT_REPOSITORY https://github.com/OxiDD/oxidd.git
    GIT_TAG v0.9.0
    CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/external
)

set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CFLAGS "-O3 -DNDEBUG")

# Compile source files into object files
# C files
set(SRCS hoalexer.c hoaparser.c simplehoa.c)

foreach(src ${SRCS})
    get_filename_component(name ${src} NAME_WE)
    add_library(${name} OBJECT ${src})
    set_target_properties(${name} PROPERTIES COMPILE_FLAGS "${CFLAGS}")
endforeach()

# C++ files
file(GLOB CXX_SOURCES "*.cpp")
add_library(main OBJECT ${CXX_SOURCES})
set_target_properties(main PROPERTIES COMPILE_FLAGS "${CFLAGS}")
target_include_directories(main PRIVATE ${CMAKE_BINARY_DIR}/external/include)

add_executable(pgsolver $<TARGET_OBJECTS:main> $<TARGET_OBJECTS:simplehoa> $<TARGET_OBJECTS:hoaparser>)
target_link_libraries(pgsolver ${CMAKE_BINARY_DIR}/external/lib/liboxidd.a)