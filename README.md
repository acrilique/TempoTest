# Tester for @michaelkrzyzaniak beat/tempo tracking library.

In order to obtain the best parameters for different kinds of music, understand how the algorithms work and change them to obtain better results, I decided to make this simple app based on raylib. 

To use it, first clone this repository (option --recurse-submodules is needed to get the dependencies too):

`git clone --recurse-submodules https://github.com/acrilique/TempoTest.git`

You will need to have raylib and soundio libraries installed in your system.

After that, go to the TempoTest directory and run:
```
make
./tester
```

## Troubleshooting

If you have problems building related to raugyi.h or gui_window_file_dialog.h files, it might be because the version of raylib that's installed in your system is different than mine. You might be able to look for those 2 files inside the raylib and raygui source directories of the version of raylib that you have installed.