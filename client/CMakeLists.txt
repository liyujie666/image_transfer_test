cmake_minimum_required(VERSION 3.12)
project(client LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB_RECURSE SRC_CLIENT 
    client.cpp
    utils/*.cpp
)

add_executable(client ${SRC_CLIENT})

set(X86_SDK_ROOT /home/liyujie/3rdparty/x86)

# opencv
set(OPENCV_ROOT ${X86_SDK_ROOT}/opencv)
set(OPENCV_INCLUDE_DIR ${OPENCV_ROOT}/include/opencv4)
set(OPENCV_LIB_DIR     ${OPENCV_ROOT}/lib)

# zmq
set(ZMQ_ROOT ${X86_SDK_ROOT}/zmq)
set(ZMQ_INCLUDE_DIR ${ZMQ_ROOT}/include)
set(ZMQ_LIB_DIR     ${ZMQ_ROOT}/lib)

# msgpack
set(MSGPACK_ROOT ${X86_SDK_ROOT}/msgpack)
set(MSGPACK_INCLUDE_DIR ${MSGPACK_ROOT}/include)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${OPENCV_INCLUDE_DIR}
    ${ZMQ_INCLUDE_DIR}
    ${MSGPACK_INCLUDE_DIR}
    
)

target_link_directories(${PROJECT_NAME} PRIVATE
    ${OPENCV_LIB_DIR}
    ${ZMQ_LIB_DIR}
    
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    # opencv
    opencv_core opencv_imgproc opencv_highgui opencv_videoio opencv_imgcodecs
    #zmq
    zmq  
    # sys
    pthread m
)


