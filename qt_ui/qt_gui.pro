#-------------------------------------------------
#
# Project created by QtCreator 2015-11-05T15:42:45
#
#-------------------------------------------------

QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = qt
#TEMPLATE = app
TEMPLATE = lib
CONFIG += staticlib

SOURCES +=\
    startup.cpp

HEADERS  += startup.cpp

FORMS    += startupwindow.ui \
    startuploadingdialog.ui
