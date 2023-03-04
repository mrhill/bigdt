cmake_minimum_required(VERSION 3.14)

project(dt LANGUAGES CXX)

#find_package(babel PATHS ../babel)
#find_library(BABEL_LIB NAMES babel PATHS ../babel/build NO_DEFAULT_PATH)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core)

include_directories (../babel/include)
include_directories (include/dt)
file(GLOB HEADERS "include/dt/*.h")
file(GLOB CPP_SOURCES "src/*.cpp")

add_library (dt ${CPP_SOURCES} ${HEADERS})
target_link_libraries(dt PRIVATE Qt${QT_VERSION_MAJOR}::Core babel)

#include_directories (include)
#add_executable(buffertest samples/buffertest/buffertest.cpp)
#add_test(buffertest COMMAND buffertest -L buffertest.log)