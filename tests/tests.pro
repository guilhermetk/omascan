QT += core gui dbus testlib
CONFIG += c++17 console testcase
TARGET = omascan-tests
TEMPLATE = app
INCLUDEPATH += ../src
HEADERS += ../src/pages.h ../src/exporter.h ../src/keys.h ../src/scanner.h ../src/cli.h
SOURCES += tst_omascan.cpp ../src/pages.cpp ../src/exporter.cpp ../src/keys.cpp ../src/scanner.cpp ../src/cli.cpp
DEFINES += OMASCAN_ROOT=\\\"$$clean_path($$PWD/..)\\\"
