# Target system name
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# Compilers
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)

# Target environment
set(ENV{PKG_CONFIG_LIBDIR} "/usr/x86_64-w64-mingw32/sys-root/mingw/lib/pkgconfig")
set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32/sys-root/mingw)

# Search behavior
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
