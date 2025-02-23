# Target system name
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# Target environment
set(ENV{PKG_CONFIG_LIBDIR} "/usr/x86_64-w64-mingw32/sys-root/mingw/lib/pkgconfig")
set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32/sys-root/mingw)
