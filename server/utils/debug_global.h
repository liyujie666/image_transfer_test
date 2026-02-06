#pragma once

/**
 * 传输方式
 * 1 = tcp
 * 2 = zmq
*/
#define IMGAE_TRANSFER_TYPE 1

/**
 * 画面采集方式
 * 1 = 本地图片
 * 2 = 摄像头
 * 3 = FFMPEG
*/
#define FRAME_CAPTURE_TYPE 3

/**
 * 摄像头采集方式
 * 1 = V4l2
 * 2 = OpenCV
 * 3 = FFmpeg
*/
#define CAMERA_CAPTURE_TYPE 3

/**
 * 颜色空间转换方式(读取本地图片时需设置)
 * 1 = OpenCV
 * 2 = rga
*/
#define COLOR_SPACE_CONVERTION_TYPE 1

/**
 * 编码方式
 * 1 = MPP
 * 2 = OpenCV
*/
#define ENCODE_TYPE 1




// 传输方式
#if IMGAE_TRANSFER_TYPE == 1
    #define TRANSFER_BY_TCP 1
#else
    #define TRANSFER_BY_TCP 0
#endif

#if IMGAE_TRANSFER_TYPE == 2
    #define TRANSFER_BY_ZMQ 1
#else
    #define TRANSFER_BY_ZMQ 0
#endif

// 采集方式
#if FRAME_CAPTURE_TYPE == 1
    #define READ_BY_LOCAL_IMAGE 1
#else
    #define READ_BY_LOCAL_IMAGE 0
#endif

#if FRAME_CAPTURE_TYPE == 2
    #define READ_BY_CAMERA 1
#else
    #define READ_BY_CAMERA 0
#endif

#if FRAME_CAPTURE_TYPE == 3
    #define READ_BY_FFMPEG 1
#else
    #define READ_BY_FFMPEG 0
#endif


// 摄像头采集方式
#if CAMERA_CAPTURE_TYPE == 1
    #define CAPTURE_BY_V4L2 1
#else
    #define CAPTURE_BY_V4L2 0
#endif

#if CAMERA_CAPTURE_TYPE == 2
    #define CAPTURE_BY_OPENCV 1
#else
    #define CAPTURE_BY_OPENCV 0
#endif

#if CAMERA_CAPTURE_TYPE == 3
    #define CAPTURE_BY_FFMPEG 1
#else
    #define CAPTURE_BY_FFMPEG 0
#endif

// 颜色空间转换方式
#if COLOR_SPACE_CONVERTION_TYPE == 1
    #define CONVERT_BY_OPENCV 1
#else
    #define CONVERT_BY_OPENCV 0
#endif

#if COLOR_SPACE_CONVERTION_TYPE == 2
    #define CONVERT_BY_RGA 1
#else
    #define CONVERT_BY_RGA 0
#endif

// 编码方式
#if ENCODE_TYPE == 1
    #define ENCODE_BY_MPP 1
#else
    #define ENCODE_BY_MPP 0
#endif

#if ENCODE_TYPE == 2
    #define ENCODE_BY_OPENCV 1
#else
    #define ENCODE_BY_OPENCV 0
#endif