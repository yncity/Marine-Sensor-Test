cmake_minimum_required(VERSION 3.5)

project(sdkExample LANGUAGES CXX)

include_directories( ${PROJECT_SOURCE_DIR}/islSdk/src)
include_directories( ${PROJECT_SOURCE_DIR}/src)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR}/bin)

#set(X32_BIT TRUE)

if (X32_BIT)
    add_definitions(-DSYSTEM_32BIT)
    message(STATUS "App build config 32 bit")  
else()
    add_definitions(-DSYSTEM_64BIT)
    message(STATUS "App build config 64 bit") 
endif()

if (WIN32)
    add_definitions(-DOS_WINDOWS)
    message(STATUS "App build config Windows")
elseif (UNIX)
    add_definitions(-DOS_UNIX)
    message(STATUS "App build config Unix")
endif()

find_package(OpenCV REQUIRED COMPONENTS core highgui imgproc videoio)

set(HEADERS
    src/app.h
    src/imuManager.h
    src/isa500App.h
    src/isd4000App.h
    src/ism3dApp.h
    src/sonarApp.h
    src/platform.h
    src/gpsApp.h
)

set(SOURCES
    src/main.cpp
    src/app.cpp
    src/imuManager.cpp
    src/isa500App.cpp
    src/isd4000App.cpp
    src/ism3dApp.cpp
    src/sonarApp.cpp
    src/platform.cpp
    src/gpsApp.cpp
)

add_subdirectory(islSdk)
add_executable (${PROJECT_NAME} ${SOURCES} ${HEADERS})
target_include_directories(${PROJECT_NAME} PRIVATE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE islSdk ${OpenCV_LIBS})

# ISLOG replay viewer
add_executable(sonarReplay src/sonarReplay.cpp)

target_include_directories(
    sonarReplay PRIVATE ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(
    sonarReplay PRIVATE islSdk ${OpenCV_LIBS}
)