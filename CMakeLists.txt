cmake_minimum_required(VERSION 3.8)
project(homegear_influxdb)

set(CMAKE_CXX_STANDARD 11)

set(SOURCE_FILES
        src/GD/GD.cpp
        src/GD/GD.h
        src/Database.cpp
        src/Database.h
        src/IpcClient.cpp
        src/IpcClient.h
        src/main.cpp
        src/Settings.cpp
        src/Settings.h
        config.h)

add_library(homegear_influxdb ${SOURCE_FILES})