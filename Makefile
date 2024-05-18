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

SOURCES = $(wildcard $(BTT_SOURCES_DIR)*.c) $(wildcard $(TOPDIR)/*.c)
OBJECTS = $(patsubst %.c,%.o,$(SOURCES))
TARGET = $(TOPDIR)/wave

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) $(OBJECTS) -o $(TARGET) -lm

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@ -lm

clean:
	rm -f $(OBJECTS) $(TARGET)

.PHONY: all clean
