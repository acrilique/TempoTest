# Makefile that allows moving the whole directory
# or invoking make from outside this directory

# Important directories for our build
MKDIR := $(abspath $(lastword $(MAKEFILE_LIST)))
TOPDIR := $(patsubst %/,%,$(dir $(MKDIR)))
BUILD_DIR := $(TOPDIR)/build/
BTT_DIR := $(TOPDIR)/lib/Beat-and-Tempo-Tracking/
BTT_SOURCES_DIR := $(BTT_DIR)src/

CC = gcc # C compiler

CFLAGS = -Wall -Wextra -pedantic -std=c17 -g # 0 warnings is what we aim for

C_LIBS = -lm -lraylib

SOURCES = $(wildcard $(BTT_SOURCES_DIR)*.c) $(wildcard $(TOPDIR)/*.c)
OBJECTS = $(patsubst %.c,%.o,$(SOURCES))
TARGET = $(TOPDIR)/tester

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) $(OBJECTS) -o $(TARGET) $(C_LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@ $(C_LIBS)

clean:
	rm -f $(OBJECTS) $(TARGET)

.PHONY: all clean
