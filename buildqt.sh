#!/usr/bin/env bash

QTBUILDDIR=build-qt_gui-Desktop_Qt_5_5_1_GCC_64bit-Debug

mkdir -p "$QTBUILDDIR"
cd "$QTBUILDDIR"
qmake ../qt_ui/qt_gui.pro
make
cd -

clang src/netcheckers.c src/network.c "$QTBUILDDIR"/*.o \
	  -Wall -Wno-missing-braces \
	  -L"$QTBUILDDIR" -lstdc++ -lQt5Core -lQt5Gui -lQt5Widgets -lqt \
	  -lSDL2 -lSDL2_image \
	  -o netcheckers
