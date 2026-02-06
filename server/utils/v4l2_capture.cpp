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

V4L2Capture::V4L2Capture(const std::string& dev_node, uint32_t pixel_format,int width, int height, int buffer_count)
    : m_dev_node(dev_node),
      m_pixel_format(pixel_format),
      m_width(width),
      m_height(height),
      m_buffer_count(buffer_count),
      m_fd(-1),
      m_is_initialized(false) {
    // 初始化缓冲区容器（避免越界）
    m_mptr.resize(m_buffer_count, std::vector<void*>(VIDEO_MAX_PLANES, nullptr));
    m_mlen.resize(m_buffer_count, std::vector<int>(VIDEO_MAX_PLANES, 0));
}

V4L2Capture::~V4L2Capture() {
    close();
}

bool V4L2Capture::init() {
    std::lock_guard<std::mutex> lock(m_mutex);

    LOG_INFO("初始化摄像头");

    // 打开设备
    m_fd = open(m_dev_node.c_str(),O_RDWR);
    if(m_fd < 0){
        LOG_ERROR("摄像头打开失败");
        return false;
    }

    // 设置多平面视频格式
    struct v4l2_format fmt = {0};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    fmt.fmt.pix_mp.width = m_width;
    fmt.fmt.pix_mp.height = m_height;
    fmt.fmt.pix_mp.pixelformat = m_pixel_format;
    fmt.fmt.pix_mp.num_planes = 2;

    if(ioctl(m_fd,VIDIOC_S_FMT,&fmt) < 0){
        LOG_ERROR("视频格式设置失败");
        ::close(m_fd);
        m_fd = -1;
        return false;
    }

    printf("V4L2Capture: 实际生效格式 - 分辨率：%dx%d，格式：0x%x\n",
               fmt.fmt.pix_mp.width, fmt.fmt.pix_mp.height, fmt.fmt.pix_mp.pixelformat);
        m_width = fmt.fmt.pix_mp.width;  // 更新为实际宽度
        m_height = fmt.fmt.pix_mp.height;// 更新为实际高度

     // 申请mmap缓冲区
     struct v4l2_requestbuffers req = {0};
     req.count = m_buffer_count;
     req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
     req.memory = V4L2_MEMORY_MMAP;

    if(ioctl(m_fd,VIDIOC_REQBUFS,&req) < 0){
        LOG_ERROR("mmap缓冲区申请失败");
        ::close(m_fd);
        m_fd = -1;
        return false;
    }
     
    // 映射每个缓冲区的每个平面
    for(int i=0;i < m_buffer_count;i++){
        struct v4l2_buffer buf = {0};
        struct v4l2_plane planes[VIDEO_MAX_PLANES] = {0};

        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        buf.length = VIDEO_MAX_PLANES;
        buf.m.planes = planes;

        if (ioctl(m_fd, VIDIOC_QUERYBUF, &buf) < 0) {
            LOG_ERROR("VIDIOC_QUERYBUF failed");
            close(); // 出错后释放已分配资源
            return false;
        }

        // 映射每个平面
        for (int p = 0; p < buf.length; ++p) {
            m_mlen[i][p] = buf.m.planes[p].length;
            m_mptr[i][p] = mmap(nullptr, m_mlen[i][p],
                               PROT_READ | PROT_WRITE,
                               MAP_SHARED, m_fd,
                               buf.m.planes[p].m.mem_offset);

            if (m_mptr[i][p] == MAP_FAILED) {
                LOG_ERROR("mmap缓冲区映射失败");
                close();
                return false;
            }
        }
    }

    // 缓冲区入队
    for (int i = 0; i < m_buffer_count; ++i) {
        struct v4l2_buffer buf = {0};
        struct v4l2_plane planes[VIDEO_MAX_PLANES] = {0};

        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        buf.length = VIDEO_MAX_PLANES;
        buf.m.planes = planes;

        if (ioctl(m_fd, VIDIOC_QBUF, &buf) < 0) {
            LOG_ERROR("VIDIOC_QBUF failed");
            close();
            return false;
        }
    }

    // 起流
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (ioctl(m_fd, VIDIOC_STREAMON, &type) < 0) {
        LOG_ERROR("VIDIOC_STREAMON failed");
        close();
        return false;
    }

    m_is_initialized = true;

    LOG_INFO("初始化摄像头成功");
    printf("设备：%s，分辨率：%dx%d\n",m_dev_node.c_str(), m_width, m_height);
    return true;

}


bool V4L2Capture::captureFrame(std::vector<uint8_t>& frame_nv12) {
    std::lock_guard<std::mutex> lock(m_mutex); 
    if(!m_is_initialized || m_fd < 0){
        LOG_ERROR("设备未初始化或已关闭");
        return false;
    }

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

    frame_nv12.create(m_height * 3 / 2, m_width, CV_8UC1);
    if (frame_nv12.empty()) {
        LOG_ERROR("创建NV12 Mat失败");
        ioctl(m_fd, VIDIOC_QBUF, &buf);
        return false;
    }

    // 拼接NV12数据到Mat的内存中
    size_t offset = 0;
    uint8_t* mat_data = frame_nv12.data;
    for(int p=0;p < buf.length;++p){
        memcpy(mat_data + offset, m_mptr[buf.index][p], buf.m.planes[p].bytesused);
        offset += buf.m.planes[p].bytesused;
    }

    // 缓冲区入队，继续采集
    if (ioctl(m_fd, VIDIOC_QBUF, &buf) < 0) {
        LOG_ERROR("VIDIOC_QBUF failed"); 
        return false;
    }

    return true;
}

void V4L2Capture::close() {
    if (!m_is_initialized) return;
    // 停流
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    ioctl(m_fd, VIDIOC_STREAMOFF, &type);

    // 解除内存映射
    for (int i = 0; i < m_buffer_count; ++i) {
        for (int p = 0; p < VIDEO_MAX_PLANES; ++p) {
            if (m_mptr[i][p] != nullptr) {
                munmap(m_mptr[i][p], m_mlen[i][p]);
                m_mptr[i][p] = nullptr;
                m_mlen[i][p] = 0;
            }
        }
    }

    // 关闭设备
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }

    m_is_initialized = false;
    LOG_INFO("已停止采集，资源释放完成");
}