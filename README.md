# Cross-platform tester for @michaelkrzyzaniak beat/tempo tracking library.

In order to obtain the best parameters for different kinds of music, understand how the algorithms work and change them to obtain better results, I decided to make this simple app.

To use it, first clone this repository (option --recurse-submodules is needed to get the `Beat-and-Tempo-Tracking` dependency too):

`git clone --recurse-submodules https://github.com/acrilique/TempoTest.git`

To build this, you will need to install pkg-config and the GTK4 development libraries. If they're not installed, the `cmake ..` command will fail with an error message. If you're on Linux, use one of the following commands to install them:

Ubuntu:
```
sudo apt install pkg-config libgtk-4-dev
```
Fedora:
```
sudo dnf install pkg-config gtk4-devel
```
OpenSUSE:
```
sudo zypper install pkg-config gtk4-devel
```

After that, go to the TempoTest directory, make a build directory, run cmake and then make:
```
mkdir builddir
cmake ..
make
./Tester audio_file.extension
```
Supported extensions are .wav, .flac and .mp3.
