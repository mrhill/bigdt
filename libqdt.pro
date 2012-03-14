TEMPLATE = lib
CONFIG += staticlib
TARGET = qdt
DEPENDPATH += .
INCLUDEPATH += ./include/dt ../../babel/include
DEFINES += bbQT

win32:QMAKE_CXXFLAGS += /Zc:wchar_t /Zp4
win32:QMAKE_CXXFLAGS_DEBUG += /Zc:wchar_t /Zp4

# Input
HEADERS += \
    include/dt/*.h

SOURCES += \
    src/*.cpp
