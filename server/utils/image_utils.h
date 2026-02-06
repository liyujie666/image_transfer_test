#pragma once

#include <opencv2/opencv.hpp>
#include <vector>
#include <cstdint>
#include "data_type.h"


bool loadImageByOpencv(const std::string& imagePath, cv::Mat& img);
bool loadVideoByOpencv(const std::string& devNode,cv::Mat& srcImg);
bool loadVideoByV4l2(const std::string& devNode,cv::Mat& srcImg);
bool loadVideoByV4l2(const std::string& devNode,std::vector<uint8_t>& srcImg);
bool loadVideoByFFmpeg(const std::string& devNode,cv::Mat& srcImg);
bool loadVideoByFFmpeg(const std::string& devNode,std::vector<uint8_t>& srcImg);
/**
 * @brief BGR -> YUV420P (I420)
 */
bool convertBgrToYuv420pByCv(const cv::Mat& bgr, cv::Mat& yuv420);
/**
 * @brief BGR -> YUV420P (I420)
 */
bool convertBgrToYuv420pByRga(const cv::Mat& bgr, cv::Mat& yuv420);
/**
 * @brief BGR -> NV12
 */
bool convertBgrToNV12ByRga(const cv::Mat& bgr, cv::Mat& nv12);
/**
 * @brief NV12 -> BGR
 */
bool convertNv12ToBgrByRga(const cv::Mat& nv12, cv::Mat& bgr);
/**
 * @brief Encode by ffmpeg (mjpeg_rkmpp cv::Mat)
 */
bool encodeImageByFFmpeg(const int& srcWidth,const int& srcHeight,const cv::Mat& srcImg,std::vector<uchar>& outData,ImageMeta& meta);

/**
 * @brief Encode by ffmpeg (mjpeg_rkmpp vector<uint8_t>)
 */
bool encodeImageByFFmpeg(const int& srcWidth,const int& srcHeight,const std::vector<uint8_t>& srcImg,std::vector<uchar>& outData,ImageMeta& meta);
/**
 * @brief Encode by opencv with Mat
 */
bool encodeImageByOpencv(const cv::Mat& srcImg,std::vector<uchar>& outData,ImageMeta& meta);


