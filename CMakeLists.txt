cmake_minimum_required(VERSION 3.14)

project(dt LANGUAGES CXX)

#find_package(babel PATHS ../babel)
#find_library(BABEL_LIB NAMES babel PATHS ../babel/build NO_DEFAULT_PATH)

include_directories (../babel/include)
include_directories (include/dt)
file(GLOB HEADERS "include/dt/*.h")
file(GLOB CPP_SOURCES "src/*.cpp")

add_library (dt ${CPP_SOURCES} ${HEADERS})

#include_directories (include)
#add_executable(buffertest samples/buffertest/buffertest.cpp)
#add_test(buffertest COMMAND buffertest -L buffertest.log)