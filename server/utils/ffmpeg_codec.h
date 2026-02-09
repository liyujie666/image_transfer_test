#pragma once

#include <vector>
#include <mutex>
#include <opencv2/opencv.hpp>
#include "data_type.h"
extern"C"{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/time.h>
}

enum class CodecType{
    H264_RKMPP,
    HEVC_RKMPP,
    MJPEG_RKMPP,
};

struct CodecParams{
    int width;
    int height;
    int fps;
    int bitrate;
    enum AVPixelFormat pixfmt;
    CodecType codec_type;
};

class FFmpegCodec{

public:
    FFmpegCodec();
    ~FFmpegCodec();

    bool initEncoder(const CodecParams& codecParams);
    bool initDecoder(CodecType codecType);

    bool encode(const std::vector<uint8_t>& frame_nv12,AVPacket** outPkt);
    bool encode(const cv::Mat& frame_nv12, AVPacket** outPkt);
    bool decode(AVPacket* pkt,std::vector<uint8_t>& frame_nv12);

    bool flushEncoder(std::vector<AVPacket*>& outPkts);

    void releaseEncoder();
    void releaseDecoder();

    CodecParams getCodecParams() const;
    std::string codecTypeToString(const CodecType& type);

private:
    bool checkRet(int ret, const std::string& err_msg);

private:

    CodecParams m_codec_params;
    // encoder params
    AVCodecContext* m_encode_ctx = nullptr;
    const AVCodec* m_encoder = nullptr;
    AVFrame* m_enc_frame = nullptr;
    bool m_is_enc_inited = false;
    int m_enc_frame_idx = 0; 
    // encoder params
    AVCodecContext* m_decode_ctx = nullptr;
    const AVCodec* m_decoder = nullptr;
    AVFrame* m_dec_frame = nullptr;
    bool m_is_dec_inited = false;

    std::mutex m_encode_mutex;
    std::mutex m_decode_mutex;


};