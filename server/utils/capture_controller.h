#pragma once

#include "data_type.h"
#include "v4l2_capture.h"
#include "ffmpeg_utils.h"
#include "rpc_engine.h"
#include <opencv2/opencv.hpp>
#include <vector>
#include <cstdint>
#include <thread>


class CaptureController{
public:
    CaptureController(const std::string& dev_node);
    ~CaptureController();

    int init(const CameraConfig& config);
    int init_devices();
    int init_encoders();

    int capture_frame_single(std::vector<uint8_t>& dst_frame, ImageMeta& meta);
    int start_capture_multiple();
    void stop_capture_multiple();
    int start_rtsp_pusher(const CameraConfig& config);
    void stop_rtsp_pusher();

    void release();
    void set_pub_stream_id(uint64_t stream_id) { pub_stream_id_ = stream_id; }

private:
    int capture_single_by_opencv(cv::Mat& src_frame);
    int capture_single_by_v4l2(cv::Mat& src_frame);
    int capture_single_by_ffmpeg(cv::Mat& src_frame);

    int encode_by_ffmpeg(const cv::Mat& src_frame,std::vector<uint8_t>& dst_frame, ImageMeta& meta);
    int encode_by_opencv(const cv::Mat& src_frame,std::vector<uint8_t>& dst_frame, ImageMeta& meta);

    std::string dev_node_;
    CameraConfig cur_config_;
    std::unique_ptr<V4L2Capture> v4l2_cap_;
    std::unique_ptr<FFmpegUtils> ffmpeg_cap_;
    std::unique_ptr<FFmpegUtils> ffmpeg_enc_;
    std::unique_ptr<FFmpegUtils> ffmpeg_pusher_;
    std::unique_ptr<cv::VideoCapture> opencv_cap_;

    // 连续采集相关
    std::atomic<bool> is_multiple_capturing_ = false; 
    std::thread capture_multiple_thread_;             
    std::mutex capture_mtx_;                         
    uint64_t pub_stream_id_ = 0;   

    // rtsp
    std::atomic<bool> is_pushing_ = false; 
    std::thread push_thread_;
    std::mutex push_mutex_;                   

    std::vector<int> encode_params_opencv{cv::IMWRITE_JPEG_QUALITY, COMPRESS_QUALITY};
    void capture_multiple_loop();
    bool publish_frame(const std::vector<uint8_t>& frame_data, const ImageMeta& meta);
};
