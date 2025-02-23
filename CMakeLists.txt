cmake_minimum_required(VERSION 3.14)
project(Tester C)

set(CMAKE_C_STANDARD 99)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

# Set output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE})

set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -Wall -Wextra -g")

set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3")

# Add the executable
add_executable(${PROJECT_NAME} main.c)

# Add the lib directory to include paths
include_directories(${CMAKE_SOURCE_DIR}/lib)

# Add the Beat-and-Tempo-Tracking directory to include paths
include_directories(${CMAKE_SOURCE_DIR}/lib/Beat-and-Tempo-Tracking)

# Add the src directory to include paths
include_directories(${CMAKE_SOURCE_DIR}/lib/Beat-and-Tempo-Tracking/src)

# Add source files
set(SOURCES
    lib/Beat-and-Tempo-Tracking/src/BTT.c
    lib/Beat-and-Tempo-Tracking/src/STFT.c
    lib/Beat-and-Tempo-Tracking/src/DFT.c
    lib/Beat-and-Tempo-Tracking/src/fastsin.c
    lib/Beat-and-Tempo-Tracking/src/Filter.c
    lib/Beat-and-Tempo-Tracking/src/Statistics.c
    audio_queue.c
    circular_buffer.c
)

# Add the source files to the executable
target_sources(${PROJECT_NAME} PRIVATE ${SOURCES})

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBGTK4 REQUIRED IMPORTED_TARGET gtk4)
target_link_libraries(${PROJECT_NAME} PkgConfig::LIBGTK4 m)

# Copy CSS file to output directory for all platforms
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/style.css"
        "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/style.css"
    COMMENT "Copying style.css to output directory"
)
