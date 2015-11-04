#!/usr/bin/env bash

clang src/netcheckers.c src/network.c -Wall -Wno-missing-braces -lSDL2 -lSDL2_image -o netcheckers
