#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <linux/videodev2.h>
#include <opencv2/core/core.hpp>
#include "image_utils.h"
#include "timer_util.h"

class V4L2Capture{
public:
    V4L2Capture(const std::string& dev_node = "/dev/video31",
                int width = 2400,
                int height = 2000,
                bool use_dma = false,
                uint32_t pixel_format = V4L2_PIX_FMT_NV12,
                int buffer_count = 4
                );
    ~V4L2Capture();

    bool init();
   
    bool captureFrame(std::vector<uint8_t>& frame_nv12);
    bool captureFrame(cv::Mat& frame_nv12);
    bool captureFrame(int& fd);     // DMA
    void close();

    int width() const { return m_width;};
    int height() const { return m_height; }
    std::string dev_node() const { return m_dev_node; }
    bool isInitialized() const { return m_is_initialized; }
    bool isUseDma() const { return m_use_dma; }

private:
    std::string m_dev_node;
    int m_fd;
    int m_width;
    int m_height;
    int m_buffer_count;         // 缓冲区的数量
    uint32_t m_pixel_format;    // 像素格式
    bool m_is_initialized;
    bool m_use_dma;
    std::mutex m_mutex;

    // 非DMA（MMAP）相关
    std::vector<std::vector<void*>> m_mptr;  // [buf_idx][plane_idx] = 映射地址
    std::vector<std::vector<int>> m_mlen;    // [buf_idx][plane_idx] = 平面长度
    // DMA（DMABuf）相关
    std::vector<std::vector<int>> m_dma_buf_fds;  // [buf_idx][plane_idx] = DMABuf fd
    std::vector<std::vector<size_t>> m_dma_buf_sizes; // [buf_idx][plane_idx] = 平面大小

    std::unique_ptr<TimerUtil> timer_;
    inline int align16(int size) { return (size + 15) & ~15; }
};