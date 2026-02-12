cmake_minimum_required(VERSION 3.12)
set(CMAKE_TOOLCHAIN_FILE /home/liyujie/3rdparty/toolchain/toolchain_rk3588.cmake)
project(server LANGUAGES CXX)
add_compile_definitions(MSGPACK_NO_BOOST)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB_RECURSE SRC_FILES
    server.cpp
    utils/*.cpp
)

add_executable(${PROJECT_NAME} ${SRC_FILES})

set(AARCH64_SDK_ROOT  ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty)

# opencv
set(OPENCV_VERSION 4.12.0)
set(OPENCV_ROOT ${AARCH64_SDK_ROOT}/opencv/${OPENCV_VERSION})
set(OPENCV_INCLUDE_DIR ${OPENCV_ROOT}/include/opencv4)
set(OPENCV_LIB_DIR     ${OPENCV_ROOT}/lib)

# zmq
set(ZMQ_ROOT ${AARCH64_SDK_ROOT}/zmq)
set(ZMQ_INCLUDE_DIR ${ZMQ_ROOT}/include)
set(ZMQ_LIB_DIR     ${ZMQ_ROOT}/lib)

# ffmpeg_rockchip
set(FFMPEG_ROOT ${AARCH64_SDK_ROOT}/ffmpeg)
set(FFMPEG_INCLUDE_DIR ${FFMPEG_ROOT}/include)
set(FFMPEG_LIB_DIR ${FFMPEG_ROOT}/lib)

# rga
set(RGA_ROOT ${AARCH64_SDK_ROOT}/rga)
set(RGA_INCLUDE_DIR ${RGA_ROOT}/include/rga)
set(RGA_LIB_DIR     ${RGA_ROOT}/lib)


# mpp
set(MPP_ROOT ${AARCH64_SDK_ROOT}/mpp)
set(MPP_INCLUDE_DIR ${MPP_ROOT}/include)
set(MPP_LIB_DIR     ${MPP_ROOT}/lib)

# msgpack
set(MSGPACK_ROOT ${AARCH64_SDK_ROOT}/msgpack)
set(MSGPACK_INCLUDE_DIR ${MSGPACK_ROOT}/include)


target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${FFMPEG_INCLUDE_DIR}
    ${OPENCV_INCLUDE_DIR}
    ${ZMQ_INCLUDE_DIR}
    ${RGA_INCLUDE_DIR}
    ${MPP_INCLUDE_DIR}
    ${MSGPACK_INCLUDE_DIR}
    
)

target_link_directories(${PROJECT_NAME} PRIVATE
    ${FFMPEG_LIB_DIR}
    ${OPENCV_LIB_DIR}
    ${ZMQ_LIB_DIR}
    ${RGA_LIB_DIR}
    ${MPP_LIB_DIR}
    
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    # ffmpeg
    avdevice avcodec avformat avutil swscale swresample
    # opencv
    opencv_core opencv_imgproc opencv_highgui opencv_videoio opencv_imgcodecs
    # mpp
    rockchip_mpp
    #zmq
    zmq 
    # rga
    rga 
    # sys
    pthread m
)


include(GNUInstallDirs)

install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# FFmpeg
install(DIRECTORY ${FFMPEG_LIB_DIR}/
    DESTINATION ${CMAKE_INSTALL_LIBDIR}
    FILES_MATCHING PATTERN "*.so*"
)

# OpenCV
install(DIRECTORY ${OPENCV_LIB_DIR}/
    DESTINATION ${CMAKE_INSTALL_LIBDIR}
    FILES_MATCHING PATTERN "*.so*"
)

# ZMQ
install(DIRECTORY ${ZMQ_LIB_DIR}/
    DESTINATION ${CMAKE_INSTALL_LIBDIR}
    FILES_MATCHING PATTERN "*.so*"
)

# MPP
install(DIRECTORY ${MPP_LIB_DIR}/
    DESTINATION ${CMAKE_INSTALL_LIBDIR}
    FILES_MATCHING PATTERN "*.so*"
)

# RGA
install(DIRECTORY ${RGA_LIB_DIR}/
    DESTINATION ${CMAKE_INSTALL_LIBDIR}
    FILES_MATCHING PATTERN "*.so*"
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    INSTALL_RPATH "$ORIGIN/../lib"
    BUILD_WITH_INSTALL_RPATH ON
)


