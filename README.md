# Cross-platform tester for @michaelkrzyzaniak beat/tempo tracking library.

In order to obtain the best parameters for different kinds of music, understand how the algorithms work and change them to obtain better results, I decided to make this simple app. Supported audio file extensions are .wav, .flac and .mp3.

## Installation

If you're looking to give it a quick test, you can download one of the packages in the [releases page](https://github.com/acrilique/TempoTest/releases). Else you'll probably want to build it yourself. 

## Native build

First clone this repository (option --recurse-submodules is needed to get the `Beat-and-Tempo-Tracking` dependency too):

`git clone --recurse-submodules https://github.com/acrilique/TempoTest.git`

To build this, you will need to install cmake, pkg-config and the GTK4 development libraries. If they're not installed, the `cmake ..` command will fail with an error message. If you're on Linux, use one of the following commands to install them:

### Linux Dependencies

Ubuntu:
```bash
sudo apt install cmake pkg-config libgtk-4-dev
```
Fedora:
```bash
sudo dnf install cmake pkg-config gtk4-devel
```
OpenSUSE:
```bash
sudo zypper install cmake pkg-config gtk4-devel
```

### MacOS Dependencies
The only way I've tried this on MacOS is via [Homebrew](https://brew.sh). After completing installation of Homebrew, run the following command to obtain the dependencies:
```sh
brew install cmake pkg-config gtk4
```

### Windows Dependencies
For this you will need to install [MSYS2](https://www.msys2.org/)

After installing, from the UCRT64 shell, run the following command to install the c compiler, gtk4 and cmake:
```bash
pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-gtk4 mingw-w64-ucrt-x86_64-cmake
```

### Build commands for all OS's
Run this from the TempoTest folder, in a shell that has access to the utilities installed above (on Windows, that'll be the UCRT64 shell if you followed the steps above)
```bash
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build
cd Release
./Tester
```

## Cross-compile from Linux for Windows

You will need the mingw64 package for gtk4, I've only tried to do this on Fedora. I had to install these packages:
```bash
sudo dnf install mingw64-gcc mingw64-gtk4
```

Then the build process is the same, but on the cmake command you need to specify compiler path and toolchain file like this:
```bash
mkdir build
cd build
cmake .. -DCMAKE_C_COMPILER:FILEPATH=x86_64-w64-mingw32-gcc -DCMAKE_TOOLCHAIN_FILE:FILEPATH=/path/to/mingw-w64-x86_64.cmake
cmake --build
```

The resulting .exe file and dll's need to be in the same folder in order for the app to execute on the target windows machine. 