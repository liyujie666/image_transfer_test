#pragma once
#include "timer_util.h"
#include <rockchip/mpp_buffer.h>
#include <rockchip/mpp_frame.h>
#include <rockchip/mpp_packet.h>
#include <rockchip/mpp_buffer.h>
#include <rockchip/rk_mpi.h>
#include <cstdio>
#include <memory>
#include <functional>

namespace mpp{

enum class MppCodecType {
    H264,  // AVC
    JPEG   // MJPEG
};


class MppEncoder{
public:
    using EncodeFrameCallback = std::function<void(const uint8_t*,size_t,int,bool)>;

    MppEncoder();
    ~MppEncoder();

    void setEncodeCallback(EncodeFrameCallback callback);
    int init(int width,int height,int fps, MppCodecType codec_type);
    void encode(unsigned char* src_data,int frame_count);
    void encode_dma(int fd, int frame_count);
    void deinit();

private:
    MppCtx mpp_ctx_;
    MppApi* mpp_mpi_;
    MppBufferGroup mpp_buf_grp_;
    MppFrame mpp_frame_;
    EncodeFrameCallback encode_callback_;
    std::unordered_map<int, MppBuffer> dma_buf_cache_;
    std::unique_ptr<TimerUtil> timer_;
    MppCodecType codec_type_;

    int width_;
    int height_;
    int align_width_;
    int align_height_;
    int fps_;

    MPP_RET configureEncoder();
    inline int align16(int size) { return (size + 15) & ~15; }

};

}   // namespace mpp