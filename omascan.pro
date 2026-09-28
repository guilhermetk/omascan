QT += core gui qml quick quickcontrols2 dbus concurrent

CONFIG += c++17 release
TARGET = omascan
TEMPLATE = app

INCLUDEPATH += src

HEADERS += \
    src/exporter.h \
    src/filechooser.h \
    src/keys.h \
    src/pageimageprovider.h \
    src/pages.h \
    src/scanner.h \
    src/theme.h

SOURCES += \
    src/main.cpp \
    src/exporter.cpp \
    src/filechooser.cpp \
    src/keys.cpp \
    src/pageimageprovider.cpp \
    src/pages.cpp \
    src/scanner.cpp \
    src/theme.cpp

RESOURCES += src/resources.qrc
