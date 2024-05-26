# Tester for @michaelkrzyzaniak beat/tempo tracking library.

In order to obtain the best parameters for different kinds of music, understand how the algorithms work and change them to obtain better results, I decided to make this simple app based on raylib. 

To use it, first clone this repository (option --recurse-submodules is needed to get the dependencies too):

`git clone --recurse-submodules https://github.com/acrilique/TempoTest.git`

You will need to have raylib installed in your system. If you haven't done it already, go to TempoTest/lib/raylib and follow the instructions in the README.md file to build and install it.

After that, go to the TempoTest directory and run:
```
make
./tester
```