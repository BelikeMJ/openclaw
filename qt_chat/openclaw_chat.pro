QT += core gui network websockets

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = openclaw_chat
TEMPLATE = app

DEFINES += QT_DEPRECATED_WARNINGS

SOURCES += main.cpp

CONFIG += c++11
