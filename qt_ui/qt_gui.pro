#-------------------------------------------------
#
# Project created by QtCreator 2015-11-05T15:42:45
#
#-------------------------------------------------

QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = qt
TEMPLATE = lib
CONFIG += staticlib

HEADERS  += \
    ../src/startup_qt.cpp \
    ../src/startup.h

SOURCES += \
    ../src/startup_qt.cpp

FORMS    += startupwindow.ui \
    startuploadingdialog.ui
