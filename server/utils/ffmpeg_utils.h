#ifndef FFMPEG_UTILS_H
#define FFMPEG_UTILS_H

#include <cstdint>
#include <string>
#include <vector>
#include <mutex>
#include <opencv2/opencv.hpp>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
}

// 编解码类型枚举
enum class CodecType {
    H264_RKMPP,
    HEVC_RKMPP,
    MJPEG_RKMPP,
};

// 编码参数结构体
struct CodecParams {
    int width = 0;
    int height = 0;
    int fps = 30;
    int bitrate = 8000;
    AVPixelFormat pixfmt = AV_PIX_FMT_NV12;
    CodecType codec_type = CodecType::MJPEG_RKMPP;
};

class FFmpegUtils {
public:
    explicit FFmpegUtils(const std::string& device = "/dev/video31");
    ~FFmpegUtils();

    FFmpegUtils(const FFmpegUtils&) = delete;
    FFmpegUtils& operator=(const FFmpegUtils&) = delete;

    // 采集
    int init_capture(int width, int height);
    int capture_frame(std::vector<uint8_t>& out_nv12);
    int capture_frame(AVFrame* out_frame);
    void release_capture();

    // 编码
    int init_encoder(const CodecParams& params);
    int init_rkmpp_hwdevice();
    int encode(const std::vector<uint8_t>& src_frame, AVPacket** out_pkt);
    int encode(const cv::Mat& src_frame, AVPacket** out_pkt);
    int encode(const AVFrame* src_frame, AVPacket** out_pkt);
    int flush_encoder(std::vector<AVPacket*>& out_pkts);
    void release_encoder();

    // 解码
    int init_decoder(CodecType type);
    int decode(AVPacket* pkt, std::vector<uint8_t>& out_nv12);
    void release_decoder();
    
    int init(int width, int height);
    void release_all();

private:
    bool check_ret(int ret, const std::string& err_msg) const;
    std::string codec_to_string(CodecType type) const;

private:
    std::string _device_path;

    // -采集
    int _cap_w = 0;
    int _cap_h = 0;
    AVFormatContext* _fmt_ctx = nullptr;
    int _video_idx = -1;
    AVPacket* _pkt_cap = nullptr;
    AVFrame*  _frame_raw = nullptr;

    // 编码
    CodecParams _enc_params{};
    AVCodecContext* _enc_ctx = nullptr;
    const AVCodec* _encoder = nullptr;
    AVFrame* _enc_frame = nullptr;
    std::mutex _enc_mutex;

    // RKMPP
    AVBufferRef* _hw_device_ctx = nullptr;  
    AVFrame* _hw_frame = nullptr;        

    // 解码
    AVCodecContext* _dec_ctx = nullptr;
    const AVCodec* _decoder = nullptr;
    AVFrame* _dec_frame = nullptr;
    std::mutex _dec_mutex;
};

#endif // FFMPEG_UTILS_H
