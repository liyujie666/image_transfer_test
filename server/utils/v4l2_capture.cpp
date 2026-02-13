#include "v4l2_capture.h"
#include "logger.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <cstring>
#include <cstdio>
#include <cerrno>
#include <opencv2/imgproc/imgproc.hpp>

static inline uint64_t now_ns()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return uint64_t(ts.tv_sec) * 1000000000ULL + uint64_t(ts.tv_nsec);
}

V4L2Capture::V4L2Capture(const std::string& dev_node, int width, int height,bool use_dma,uint32_t pixel_format, int buffer_count)
    : m_dev_node(dev_node),
      m_width(width),
      m_height(height),
      m_use_dma(use_dma),
      m_pixel_format(pixel_format),
      m_buffer_count(buffer_count),
      m_fd(-1),
      m_is_initialized(false){
    timer_ = std::make_unique<TimerUtil>();
}

V4L2Capture::~V4L2Capture() {
    close();
}

bool V4L2Capture::init() {
    std::lock_guard<std::mutex> lock(m_mutex);

    m_fd = open(m_dev_node.c_str(), O_RDWR | O_CLOEXEC);
    if (m_fd < 0) {
        LOG_ERROR("打开设备失败: %s errno=%d (%s)", m_dev_node.c_str(), errno, strerror(errno));
        return false;
    }

    // 设置 format
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    fmt.fmt.pix_mp.width = m_width;
    fmt.fmt.pix_mp.height = m_height;
    fmt.fmt.pix_mp.pixelformat = m_pixel_format;
    if (ioctl(m_fd, VIDIOC_S_FMT, &fmt) < 0) {
        LOG_ERROR("VIDIOC_S_FMT failed errno=%d (%s)", errno, strerror(errno));
        ::close(m_fd);
        m_fd = -1;
        return false;
    }

    // 先设置 MMAP buffers，再导出dmabuf，直接设置为DMABUF导出会报错
    struct v4l2_requestbuffers req = {0};
    req.count = m_buffer_count;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    req.memory = V4L2_MEMORY_MMAP;

    if (ioctl(m_fd, VIDIOC_REQBUFS, &req) < 0) {
        LOG_ERROR("VIDIOC_REQBUFS(MMAP) failed errno=%d (%s)", errno, strerror(errno));
        ::close(m_fd);
        m_fd = -1;
        return false;
    }

    int actual_count = req.count;
    if (actual_count <= 0) {
        LOG_ERROR("driver returned zero buffers (req.count=%d)", actual_count);
        ::close(m_fd);
        m_fd = -1;
        return false;
    }

    // resize containers
    m_dma_buf_fds.resize(actual_count, std::vector<int>(VIDEO_MAX_PLANES, -1));
    m_dma_buf_sizes.resize(actual_count, std::vector<size_t>(VIDEO_MAX_PLANES, 0));
    m_mptr.resize(actual_count, std::vector<void*>(VIDEO_MAX_PLANES, nullptr));
    m_mlen.resize(actual_count, std::vector<int>(VIDEO_MAX_PLANES, 0));

    bool export_all_ok = true;
    int planes_per_buf = 0;

    // 申请缓冲区
    for (int i = 0; i < actual_count; ++i) {
        struct v4l2_buffer qbuf;
        memset(&qbuf, 0, sizeof(qbuf));
        struct v4l2_plane planes[VIDEO_MAX_PLANES];
        memset(planes, 0, sizeof(planes));
        qbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        qbuf.memory = V4L2_MEMORY_MMAP;
        qbuf.index = i;
        qbuf.length = VIDEO_MAX_PLANES;
        qbuf.m.planes = planes;

        if (ioctl(m_fd, VIDIOC_QUERYBUF, &qbuf) < 0) {
            LOG_ERROR("VIDIOC_QUERYBUF failed idx=%d errno=%d (%s)", i, errno, strerror(errno));
            export_all_ok = false;
            break;
        }

        // 记录实际 plane 数
        planes_per_buf = qbuf.length;

        for (int p = 0; p < qbuf.length; ++p) {
            m_mlen[i][p] = qbuf.m.planes[p].length;
            m_mptr[i][p] = mmap(nullptr, m_mlen[i][p], PROT_READ | PROT_WRITE, MAP_SHARED, m_fd,
                                qbuf.m.planes[p].m.mem_offset);
            if (m_mptr[i][p] == MAP_FAILED) {
                LOG_ERROR("初次 mmap 失败 idx=%d plane=%d errno=%d (%s)", i, p, errno, strerror(errno));
                m_mptr[i][p] = nullptr;
                export_all_ok = false;
                break;
            }
        }
        if (!export_all_ok) break;

        // 导出每个 plane -> VIDIOC_EXPBUF
        for (int p = 0; p < qbuf.length; ++p) {
            struct v4l2_exportbuffer exp;
            memset(&exp, 0, sizeof(exp));
            exp.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
            exp.index = i;
            exp.plane = p;
            exp.flags = 0;

            if (ioctl(m_fd, VIDIOC_EXPBUF, &exp) < 0) {
                LOG_WARN("VIDIOC_EXPBUF failed idx=%d plane=%d errno=%d (%s)", i, p, errno, strerror(errno));
                export_all_ok = false;
                break;
            }
            // 保存导出 fd 和 size
            m_dma_buf_fds[i][p] = exp.fd;
            m_dma_buf_sizes[i][p] = m_mlen[i][p];
            LOG_INFO("exported fd=%d idx=%d plane=%d size=%d", exp.fd, i, p, m_dma_buf_sizes[i][p]);
        }
        if (!export_all_ok) break;
    }

    if (m_use_dma && export_all_ok) {
        // 如果用户希望使用 DMABUF 且导出成功：
        // 释放刚才的 mmap（驱动上仍然持有 buffer），并把 buffers 以 DMABUF 模式重新注册并 QBUF 回驱动
        for (int i = 0; i < actual_count; ++i) {
            for (int p = 0; p < planes_per_buf; ++p) {
                if (m_mptr[i][p]) {
                    munmap(m_mptr[i][p], m_mlen[i][p]);
                    m_mptr[i][p] = nullptr;
                    m_mlen[i][p] = 0;
                }
            }
        }

        // 释放原来 MMAP 的 reqbufs
        struct v4l2_requestbuffers req_free;
        memset(&req_free, 0, sizeof(req_free));
        req_free.count = 0;
        req_free.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        req_free.memory = V4L2_MEMORY_MMAP;
        ioctl(m_fd, VIDIOC_REQBUFS, &req_free);

        // 再次以 DMABUF 请求
        struct v4l2_requestbuffers req_dmabuf;
        memset(&req_dmabuf, 0, sizeof(req_dmabuf));
        req_dmabuf.count = actual_count;
        req_dmabuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        req_dmabuf.memory = V4L2_MEMORY_DMABUF;

        if (ioctl(m_fd, VIDIOC_REQBUFS, &req_dmabuf) < 0) {
            LOG_WARN("VIDIOC_REQBUFS(DMABUF) failed errno=%d (%s) -> 回退到 MMAP", errno, strerror(errno));
            // 关闭导出的 fds
            for (int i = 0; i < actual_count; ++i)
                for (int p = 0; p < planes_per_buf; ++p)
                    if (m_dma_buf_fds[i][p] > 0) { ::close(m_dma_buf_fds[i][p]); m_dma_buf_fds[i][p] = -1; }
            m_use_dma = false; // fallback
        } else {
            // 把导出的 fd QBUF 回驱动（驱动现在以 DMABUF 模式管理）
            for (int i = 0; i < actual_count; ++i) {
                struct v4l2_buffer buf;
                memset(&buf, 0, sizeof(buf));
                struct v4l2_plane planes[VIDEO_MAX_PLANES];
                memset(planes, 0, sizeof(planes));
                buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
                buf.memory = V4L2_MEMORY_DMABUF;
                buf.index = i;
                buf.length = planes_per_buf;
                buf.m.planes = planes;
                for (int p = 0; p < planes_per_buf; ++p) {
                    buf.m.planes[p].m.fd = m_dma_buf_fds[i][p];
                    buf.m.planes[p].length = m_dma_buf_sizes[i][p];
                }
                if (ioctl(m_fd, VIDIOC_QBUF, &buf) < 0) {
                    LOG_WARN("VIDIOC_QBUF with DMABUF failed idx=%d errno=%d (%s)", i, errno, strerror(errno));
                }
            }
        }
    } else {
        // 导出失败或用户不需要 DMABUF -> 继续用 MMAP 路径，确保把所有 buffer 入队
        if (!m_use_dma) LOG_INFO("使用 MMAP 模式 (m_use_dma=false)");
        else LOG_WARN("DMABUF 导出失败，回退到 MMAP 模式");

        for (int i = 0; i < actual_count; ++i) {
            struct v4l2_buffer buf;
            memset(&buf, 0, sizeof(buf));
            struct v4l2_plane planes[VIDEO_MAX_PLANES];
            memset(planes,0,sizeof(planes));
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.index = i;
            buf.length = VIDEO_MAX_PLANES;
            buf.m.planes = planes;

            // re-query to get lengths if needed
            if (ioctl(m_fd, VIDIOC_QUERYBUF, &buf) < 0) {
                LOG_ERROR("VIDIOC_QUERYBUF (requeue) failed idx=%d errno=%d (%s)", i, errno, strerror(errno));
                // 清理并失败
                close();
                return false;
            }
            for (int p = 0; p < buf.length; ++p) {
                if (!m_mptr[i][p]) {
                    m_mlen[i][p] = buf.m.planes[p].length;
                    m_mptr[i][p] = mmap(nullptr, m_mlen[i][p],
                                        PROT_READ | PROT_WRITE, MAP_SHARED, m_fd,
                                        buf.m.planes[p].m.mem_offset);
                    if (m_mptr[i][p] == MAP_FAILED) {
                        LOG_ERROR("mmap失败 idx=%d plane=%d errno=%d (%s)", i, p, errno, strerror(errno));
                        close();
                        return false;
                    }
                }
            }

            // QBUF 入队
            if (ioctl(m_fd, VIDIOC_QBUF, &buf) < 0) {
                LOG_ERROR("VIDIOC_QBUF failed idx=%d errno=%d (%s)", i, errno, strerror(errno));
                close();
                return false;
            }
        }
    }

    // 起流
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (ioctl(m_fd, VIDIOC_STREAMON, &type) < 0) {
        LOG_ERROR("VIDIOC_STREAMON failed errno=%d (%s)", errno, strerror(errno));
        close();
        return false;
    }

    m_is_initialized = true;
    LOG_INFO("V4L2 初始化完成 (mode=%s) buffers=%d planes=%d",
             m_use_dma ? "DMABUF" : "MMAP", actual_count, planes_per_buf);

    return true;
}



bool V4L2Capture::captureFrame(std::vector<uint8_t>& frame_nv12) {
    std::lock_guard<std::mutex> lock(m_mutex); 
    if(!m_is_initialized || m_fd < 0){
        LOG_ERROR("设备未初始化或已关闭");
        return false;
    }

    timer_->start();
    // 出队取帧
    struct v4l2_buffer buf = {0};
    struct v4l2_plane planes[VIDEO_MAX_PLANES] = {0};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.length = VIDEO_MAX_PLANES;
    buf.m.planes = planes;

    if (ioctl(m_fd, VIDIOC_DQBUF, &buf) < 0) {
        LOG_ERROR("VIDIOC_DQBUF failed");
        return false;
    }

    // 拼接NV12原始数据
    frame_nv12.clear();
    size_t total_size = 0;
    for(int p=0;p < buf.length;++p){
        total_size += buf.m.planes[p].bytesused;
    }

    frame_nv12.resize(total_size);

    size_t offset = 0;
    for(int p=0;p < buf.length;++p){
        memcpy(frame_nv12.data() + offset, m_mptr[buf.index][p], buf.m.planes[p].bytesused);
        offset += buf.m.planes[p].bytesused;
    }

    timer_->end("[Capture] [V4l2]");
    // 缓冲区入队，继续采集
    if (ioctl(m_fd, VIDIOC_QBUF, &buf) < 0) {
        LOG_ERROR("VIDIOC_DQBUF failed");
        return false;
    }

    return true;
}

// V4L2Capture.cpp 中实现
bool V4L2Capture::captureFrame(cv::Mat& frame_nv12) {
    std::lock_guard<std::mutex> lock(m_mutex); 
    if(!m_is_initialized || m_fd < 0){
        LOG_ERROR("设备未初始化或已关闭");
        return false;
    }
    timer_->start();

    // 出队取帧
    struct v4l2_buffer buf = {0};
    struct v4l2_plane planes[VIDEO_MAX_PLANES] = {0};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.length = VIDEO_MAX_PLANES;
    buf.m.planes = planes;

    if (ioctl(m_fd, VIDIOC_DQBUF, &buf) < 0) {
        LOG_ERROR("VIDIOC_DQBUF failed");
        return false;
    }

    // 计算NV12总字节数
    size_t total_size = 0;
    for(int p=0;p < buf.length;++p){
        total_size += buf.m.planes[p].bytesused;
    }

    if (frame_nv12.empty() || frame_nv12.rows != m_height * 3 / 2 || frame_nv12.cols != m_width) {
        frame_nv12.create(m_height * 3 / 2, m_width, CV_8UC1);
    }

    // 拼接NV12数据到Mat的内存中
    size_t offset = 0;
    uint8_t* mat_data = frame_nv12.data;
    for(int p=0;p < buf.length;++p){
        memcpy(mat_data + offset, m_mptr[buf.index][p], buf.m.planes[p].bytesused);
        offset += buf.m.planes[p].bytesused;
    }

    timer_->end("[Capture] [V4l2]");
    // 缓冲区入队，继续采集
    if (ioctl(m_fd, VIDIOC_QBUF, &buf) < 0) {
        LOG_ERROR("VIDIOC_QBUF failed"); 
        return false;
    }

    return true;
}


bool V4L2Capture::captureFrame(int& fd){
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_is_initialized || m_fd < 0 || !m_use_dma) {
        LOG_ERROR("DMA模式未初始化或当前是非DMA模式");
        fd = -1;
        return false;
    }

    timer_->start();

    // 出队缓冲区
    struct v4l2_buffer buf = {0};
    struct v4l2_plane planes[VIDEO_MAX_PLANES] = {0};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    buf.memory = V4L2_MEMORY_DMABUF;
    buf.length = VIDEO_MAX_PLANES;
    buf.m.planes = planes;
    if (ioctl(m_fd, VIDIOC_DQBUF, &buf) < 0) {
        LOG_ERROR("DMA模式出队失败");
        return false;
    }

    // 获取缓冲区对应的DMABuf fd
    fd = m_dma_buf_fds[buf.index][0]; 
    if (fd < 0) {
        LOG_ERROR("DMA缓冲区fd无效（buf_idx=%d, plane_idx=0）", buf.index);
        ioctl(m_fd, VIDIOC_QBUF, &buf);
        return false;
    }

    // 重新入队
    if (ioctl(m_fd, VIDIOC_QBUF, &buf) < 0) {
        LOG_ERROR("DMA模式重新入队失败");
        return false;
    }

    timer_->end("[Capture] [V4L2_DMA]");
    return true;

}
void V4L2Capture::close() {
    if (!m_is_initialized) return;
    // 停流
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    ioctl(m_fd, VIDIOC_STREAMOFF, &type);

    // 解除内存映射
    for (int buf_idx = 0; buf_idx < m_buffer_count; buf_idx++) {
        for (int plane_idx = 0; plane_idx < VIDEO_MAX_PLANES; plane_idx++) {
            if (m_use_dma) {
                // DMA模式：关闭DMABuf fd
                if (m_dma_buf_fds[buf_idx][plane_idx] >= 0) {
                    ::close(m_dma_buf_fds[buf_idx][plane_idx]);
                    m_dma_buf_fds[buf_idx][plane_idx] = -1;
                }
            } else {
                // 非DMA模式：解除MMAP
                if (m_mptr[buf_idx][plane_idx] != nullptr) {
                    munmap(m_mptr[buf_idx][plane_idx], m_mlen[buf_idx][plane_idx]);
                    m_mptr[buf_idx][plane_idx] = nullptr;
                }
            }
        }
    }

    // 关闭设备
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }

    m_is_initialized = false;
    LOG_INFO(("V4L2已关闭（" + std::string(m_use_dma ? "DMA模式" : "非DMA模式") + "）").c_str());
}