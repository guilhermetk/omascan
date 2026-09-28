QT += core gui dbus testlib
CONFIG += c++17 console testcase
TARGET = omascan-tests
TEMPLATE = app
INCLUDEPATH += ../src
HEADERS += ../src/pages.h ../src/exporter.h ../src/keys.h
SOURCES += tst_omascan.cpp ../src/pages.cpp ../src/exporter.cpp ../src/keys.cpp
