TEMPLATE = lib
CONFIG += staticlib
TARGET = libqdt
DEPENDPATH += .
INCLUDEPATH += ./include/dt ../../babel/include
DEFINES += bbQT

QMAKE_CXXFLAGS += /Zc:wchar_t /Zp4
QMAKE_CXXFLAGS_DEBUG += /Zc:wchar_t /Zp4

# Input
HEADERS += \
    include/dt/*.h

SOURCES += \
    src/*.cpp
