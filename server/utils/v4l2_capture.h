#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <linux/videodev2.h>
#include <opencv2/core/core.hpp>
#include "image_utils.h"

class V4L2Capture{
public:
    V4L2Capture(const std::string& dev_node = "/dev/video31",
                int width = 2400,
                int height = 2000,
                uint32_t pixel_format = V4L2_PIX_FMT_NV12,
                int buffer_count = 4);
    ~V4L2Capture();

    bool init();
    // 返回：true=成功；frame_bgr=转换后的BGR格式（供OpenCV处理）；frame_nv12=原始NV12数据（供ZMQ传输）
    bool captureFrame(std::vector<uint8_t>& frame_nv12);
    bool captureFrame(cv::Mat& frame_nv12);
    void close();

    int width() const { return m_width;};
    int height() const { return m_height; }
    std::string dev_node() const { return m_dev_node; }
    bool isInitialized() const { return m_is_initialized; }

private:
    std::string m_dev_node;
    int m_fd;
    int m_width;
    int m_height;
    int m_buffer_count;         // 缓冲区的数量
    uint32_t m_pixel_format;    // 像素格式
    bool m_is_initialized;
    std::mutex m_mutex;

    // 多平面缓冲区数据
    std::vector<std::vector<void*>> m_mptr;  // [缓冲区索引][平面索引] = 映射地址
    std::vector<std::vector<int>> m_mlen;    // [缓冲区索引][平面索引] = 平面长度

};