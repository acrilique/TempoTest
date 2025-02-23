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

# Copy required files post-build
if(WIN32)
    # List of required DLLs for Windows
    set(REQUIRED_DLLS
        libwinpthread-1
        libcairo-2
        libcairo-gobject-2
        libcairo-script-interpreter-2
        libgio-2.0-0
        libglib-2.0-0
        libgobject-2.0-0
        libgtk-4-1
        libgcc_s_seh-1
        libstdc++-6
        libfontconfig-1
        libfreetype-6
        libpixman-1-0
        libpng16-16
        libintl-8
        libpcre2-8-0
        libgmodule-2.0-0
        libffi-8
        libepoxy-0
        libfribidi-0
        libgdk_pixbuf-2.0-0
        libgraphene-1.0-0
        libharfbuzz-0
        libjpeg-62
        libpango-1.0-0
        libpangocairo-1.0-0
        libpangoft2-1.0-0
        libpangowin32-1.0-0
        libtiff-5
        zlib1
        iconv
        libexpat-1
        libbz2-1
    )

    # Copy each required DLL
    foreach(dll ${REQUIRED_DLLS})
        set(dll_path "${CMAKE_FIND_ROOT_PATH}/bin/${dll}.dll")
        
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${dll_path}
                $<TARGET_FILE_DIR:${PROJECT_NAME}>
            COMMENT "Copying ${dll}.dll to output directory"
        )
    endforeach()
endif()

# Copy CSS file to output directory for all platforms
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/style.css"
        $<TARGET_FILE_DIR:${PROJECT_NAME}>/style.css
    COMMENT "Copying style.css to output directory"
)
