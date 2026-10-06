QT += core gui widgets printsupport qml quick quickcontrols2 quickdialogs2 dbus

CONFIG += c++17 release
TARGET = omawrite
TEMPLATE = app

HEADERS += \
    src/backend.h \
    src/markdownhighlighter.h \
    src/spellchecker.h \
    src/systemtheme.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/markdownhighlighter.cpp \
    src/spellchecker.cpp \
    src/systemtheme.cpp

RESOURCES += src/resources.qrc

CONFIG += link_pkgconfig
PKGCONFIG += hunspell
