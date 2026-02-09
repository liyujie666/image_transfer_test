#include "ffmpeg_utils.h"
#include "timer_util.h"
#include <cstring>
#include <unistd.h>
#include <iostream>
#include <mutex>
#include <opencv2/opencv.hpp>

static std::once_flag g_av_flag;
static void av_register_all_devices() {
    avdevice_register_all();
    av_log_set_level(AV_LOG_ERROR);
}

FFmpegUtils::FFmpegUtils(const std::string& device)
    : _device_path(device) {
    std::call_once(g_av_flag, av_register_all_devices);
}

FFmpegUtils::~FFmpegUtils() {
    release_all();
}

bool FFmpegUtils::check_ret(int ret, const std::string& err_msg) const {
    if (ret >= 0) return true;
    char buf[1024]{};
    av_strerror(ret, buf, sizeof(buf));
    std::cerr << "[FFmpeg] " << err_msg << ": " << buf << " (" << ret << ")" << std::endl;
    return false;
}

std::string FFmpegUtils::codec_to_string(CodecType type) const {
    switch (type) {
        case CodecType::H264_RKMPP: return "h264_rkmpp";
        case CodecType::HEVC_RKMPP: return "hevc_rkmpp";
        case CodecType::MJPEG_RKMPP: return "mjpeg_rkmpp";
        default: return "unknown";
    }
}


int FFmpegUtils::init_capture(int width, int height) {
    if (_fmt_ctx) {
        std::cerr << "采集模块已初始化" << std::endl;
        return -1;
    }
    _cap_w = width;
    _cap_h = height;

    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "video_size", (std::to_string(width) + "x" + std::to_string(height)).c_str(), 0);
    av_dict_set(&opts, "input_format", "nv12", 0);
    av_dict_set(&opts, "v4l2_multiplanar", "1", 0);
    av_dict_set(&opts, "probesize", "32M", 0);

    int ret = avformat_open_input(&_fmt_ctx, _device_path.c_str(), nullptr, &opts);
    av_dict_free(&opts);
    if (!check_ret(ret, "打开摄像头失败")) goto fail;

    ret = avformat_find_stream_info(_fmt_ctx, nullptr);
    if (!check_ret(ret, "获取流信息失败")) goto fail;

    // 查找视频流
    for (int i = 0; i < _fmt_ctx->nb_streams; i++) {
        if (_fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            _video_idx = i;
            break;
        }
    }
    if (_video_idx < 0) {
        std::cerr << "未找到视频流" << std::endl;
        goto fail;
    }

    // 分配缓冲
    _pkt_cap = av_packet_alloc();
    _frame_raw = av_frame_alloc();
    if (!_pkt_cap || !_frame_raw) goto fail;

    std::cout << "采集模块初始化成功: " << width << "x" << height << std::endl;
    return 0;

fail:
    release_capture();
    return -1;
}

int FFmpegUtils::capture_frame(std::vector<uint8_t>& out_nv12) {
    if (!_fmt_ctx || !_pkt_cap) return -1;

    TimerUtil captureTimer;
    captureTimer.start();
    int ret = av_read_frame(_fmt_ctx, _pkt_cap);
    if (!check_ret(ret, "读取帧失败")) return ret;

    if (_pkt_cap->stream_index != _video_idx) {
        av_packet_unref(_pkt_cap);
        return -2;
    }
    captureTimer.end("Capture frame by v4l2");

    // 计算NV12大小
    const int y_size = _cap_w * _cap_h;
    const int uv_size = y_size / 2;
    out_nv12.resize(y_size + uv_size);

    // 填充帧数据
    ret = av_image_fill_arrays(
        _frame_raw->data, _frame_raw->linesize,
        _pkt_cap->data, AV_PIX_FMT_NV12,
        _cap_w, _cap_h, 32
    );
    if (ret < 0) {
        av_packet_unref(_pkt_cap);
        return ret;
    }

    // 拷贝为连续内存NV12
    memcpy(out_nv12.data(), _frame_raw->data[0], y_size);
    memcpy(out_nv12.data() + y_size, _frame_raw->data[1], uv_size);

    av_frame_unref(_frame_raw);
    av_packet_unref(_pkt_cap);
    return 0;
}


int FFmpegUtils::capture_frame(cv::Mat& out_nv12) {
    if (!_fmt_ctx || !_pkt_cap) return -1;

    TimerUtil timer;
    timer.start();
    
    int ret = av_read_frame(_fmt_ctx, _pkt_cap);
    if (!check_ret(ret, "读取帧失败")) {
        return ret;
    }


    if (_pkt_cap->stream_index != _video_idx) {
        av_packet_unref(_pkt_cap);
        return -2;
    }
    //captureTimer.end("Capture frame by v4l2");

    if (_cap_w <= 0 || _cap_h <= 0) {
        av_packet_unref(_pkt_cap);
        return -3;  // 宽高无效的错误码
    }


    const int y_size = _cap_w * _cap_h;
    const int nv12_total_size = y_size * 3 / 2;

    out_nv12 = cv::Mat(cv::Size(_cap_w, _cap_h * 3 / 2), CV_8UC1);
    if (out_nv12.empty()) {
        av_packet_unref(_pkt_cap);
        return -4;  // Mat创建失败的错误码
    }

    ret = av_image_fill_arrays(
        _frame_raw->data, _frame_raw->linesize,
        _pkt_cap->data, AV_PIX_FMT_NV12,
        _cap_w, _cap_h, 32
    );
    if (ret < 0) {
        av_packet_unref(_pkt_cap);
        return ret;
    }

    // Y
    memcpy(out_nv12.data, _frame_raw->data[0], y_size);
    // UV
    memcpy(out_nv12.data + y_size, _frame_raw->data[1], y_size / 2);

    timer.end("Capture frame by ffmpeg");
    // 释放资源（原逻辑不变）
    av_frame_unref(_frame_raw);
    av_packet_unref(_pkt_cap);
    
    return 0;
}


int FFmpegUtils::capture_frame(AVFrame* out_frame){
    if (!_fmt_ctx || !_pkt_cap || !out_frame) return -1;

    TimerUtil captureTimer;
    captureTimer.start();
    int ret = av_read_frame(_fmt_ctx, _pkt_cap);
    if (!check_ret(ret, "读取帧失败")) return ret;

    if (_pkt_cap->stream_index != _video_idx) {
        av_packet_unref(_pkt_cap);
        return -2;
    }
    captureTimer.end("Capture frame by v4l2");

    av_frame_unref(out_frame);
    out_frame->width = _cap_w;
    out_frame->height = _cap_h;
    out_frame->format = AV_PIX_FMT_NV12;

    ret = av_frame_get_buffer(out_frame,32);
    if(!check_ret(ret,"分配目标帧缓存失败")){
        av_packet_unref(_pkt_cap);
        return ret;
    } 

    // 填充帧数据
    ret = av_image_fill_arrays(
        _frame_raw->data, _frame_raw->linesize,
        _pkt_cap->data, AV_PIX_FMT_NV12,
        _cap_w, _cap_h, 32
    );
    if (ret < 0) {
        av_packet_unref(_pkt_cap);
        return ret;
    }

    const int y_size = _cap_w * _cap_h;
    const int uv_size = y_size / 2;
    memcpy(out_frame->data[0], _frame_raw->data[0], y_size);
    memcpy(out_frame->data[1], _frame_raw->data[1], uv_size);

    out_frame->linesize[0] = _cap_w;
    out_frame->linesize[1] = _cap_w;

    av_packet_unref(_pkt_cap);
    av_frame_unref(_frame_raw);
    return 0;
}

void FFmpegUtils::release_capture() {
    if (_frame_raw) av_frame_free(&_frame_raw);
    if (_pkt_cap) av_packet_free(&_pkt_cap);
    if (_fmt_ctx) avformat_close_input(&_fmt_ctx);
    _video_idx = -1;
    _cap_w = _cap_h = 0;
    std::cout << "采集模块已释放" << std::endl;
}

int FFmpegUtils::init_encoder(const CodecParams& params) {
    std::lock_guard<std::mutex> lock(_enc_mutex);
    if (_enc_ctx) {
        std::cerr << "编码器已初始化" << std::endl;
        return -1;
    }

    _enc_params = params;
    const std::string name = codec_to_string(params.codec_type);
    _encoder = avcodec_find_encoder_by_name(name.c_str());

    if (!_encoder) {
        std::cerr << "找不到编码器: " << name << std::endl;
        return -1;
    }

    _enc_ctx = avcodec_alloc_context3(_encoder);
    _enc_ctx->width = params.width;
    _enc_ctx->height = params.height;
    _enc_ctx->pix_fmt = params.pixfmt;
    _enc_ctx->bit_rate = params.bitrate * 1000;
    _enc_ctx->codec_type = AVMEDIA_TYPE_VIDEO;

    // RKMPP 参数配置
    if (params.codec_type == CodecType::MJPEG_RKMPP) {
        _enc_ctx->gop_size = 1;
        _enc_ctx->max_b_frames = 0;    
        _enc_ctx->framerate = {0,1};
        _enc_ctx->time_base = {1,1};
        av_opt_set_int(_enc_ctx->priv_data, "qp_init", 50, 0);
        av_opt_set_int(_enc_ctx->priv_data, "chroma_fmt", 4, 0);
    } else {
        _enc_ctx->framerate = {params.fps, 1};
        _enc_ctx->time_base = {1, params.fps};
        _enc_ctx->gop_size = 120;
        av_opt_set(_enc_ctx->priv_data, "tune", "zerolatency", 0);
    }

    int ret = avcodec_open2(_enc_ctx, _encoder, nullptr);
    if (!check_ret(ret, "打开编码器失败")) goto fail;

    // 分配编码帧
    _enc_frame = av_frame_alloc();
    _enc_frame->width = params.width;
    _enc_frame->height = params.height;
    _enc_frame->format = params.pixfmt;
    ret = av_frame_get_buffer(_enc_frame, 0);
    if (!check_ret(ret, "分配编码帧失败")) goto fail;

    std::cout << "编码器初始化成功: " << name << std::endl;
    return 0;

fail:
    release_encoder();
    return -1;
}

int FFmpegUtils::init_rkmpp_hwdevice() {
    // 注册瑞芯微硬件设备类型
    const enum AVHWDeviceType hw_type = av_hwdevice_find_type_by_name("rkmpp");
    if (hw_type == AV_HWDEVICE_TYPE_NONE) {
        std::cerr << "不支持RKMPP硬件设备" << std::endl;
        return -1;
    }

    // 创建硬件设备上下文
    int ret = av_hwdevice_ctx_create(&_hw_device_ctx, hw_type, nullptr, nullptr, 0);
    if (!check_ret(ret, "创建RKMPP硬件上下文失败")) {
        return -1;
    }
    return 0;
}

int FFmpegUtils::encode(const std::vector<uint8_t>& src_frame, AVPacket** out_pkt) {
    std::lock_guard<std::mutex> lock(_enc_mutex);
    if (!_enc_ctx || src_frame.empty() || !out_pkt) return -1;

    const int y_size = _enc_params.width * _enc_params.height;
    const int uv_size = y_size / 2;
    if (src_frame.size() != y_size + uv_size) return -2;

    av_frame_make_writable(_enc_frame);
    memcpy(_enc_frame->data[0], src_frame.data(), y_size);
    memcpy(_enc_frame->data[1], src_frame.data() + y_size, uv_size);
    _enc_frame->linesize[0] = _enc_params.width;
    _enc_frame->linesize[1] = _enc_params.width;

    TimerUtil encodeTimer;
    encodeTimer.start();
    int ret = avcodec_send_frame(_enc_ctx, _enc_frame);
    if (ret < 0 && ret != AVERROR(EAGAIN)) return ret;

    *out_pkt = av_packet_alloc();
    ret = avcodec_receive_packet(_enc_ctx, *out_pkt);
    if (ret == AVERROR(EAGAIN)) {
        av_packet_free(out_pkt);
        *out_pkt = nullptr;
        return AVERROR(EAGAIN);
    }
    if (!check_ret(ret, "接收编码数据失败")) {
        av_packet_free(out_pkt);
        *out_pkt = nullptr;
        return ret;
    }
    encodeTimer.end("encode frame by mpp");
    return 0;
}

int FFmpegUtils::encode(const cv::Mat& src_frame, AVPacket** out_pkt) {
    std::lock_guard<std::mutex> lock(_enc_mutex);
    if (!_enc_ctx || src_frame.empty() || !out_pkt) return -1;

    const int y_size = _enc_params.width * _enc_params.height;
    av_frame_make_writable(_enc_frame);
    memcpy(_enc_frame->data[0], src_frame.data, y_size);
    memcpy(_enc_frame->data[1], src_frame.data + y_size, y_size / 2);

    TimerUtil encodeTimer;
    encodeTimer.start();
    int ret = avcodec_send_frame(_enc_ctx, _enc_frame);
    if (ret < 0 && ret != AVERROR(EAGAIN)) return ret;

    *out_pkt = av_packet_alloc();
    ret = avcodec_receive_packet(_enc_ctx, *out_pkt);
    if (ret == AVERROR(EAGAIN)) {
        av_packet_free(out_pkt);
        *out_pkt = nullptr;
        return AVERROR(EAGAIN);
    }
    if (!check_ret(ret, "接收编码数据失败")) {
        av_packet_free(out_pkt);
        *out_pkt = nullptr;
        return ret;
    }
    encodeTimer.end("encode frame by mpp");
    return 0;
}

int FFmpegUtils::encode(const AVFrame* src_frame, AVPacket** out_pkt){
    std::lock_guard<std::mutex> lock(_enc_mutex);
    if (!_enc_ctx || !src_frame || !out_pkt) return -1;

    if (src_frame->width != _enc_params.width ||
        src_frame->height != _enc_params.height ||
        src_frame->format != _enc_params.pixfmt) {
        std::cerr << "输入帧格式与编码器配置不匹配" << std::endl;
        return -2;
    }

    TimerUtil encodeTimer;
    encodeTimer.start();

    int ret = avcodec_send_frame(_enc_ctx, src_frame);
    if (ret < 0 && ret != AVERROR(EAGAIN)) {
        check_ret(ret, "发送原始帧到编码器失败");
        return ret;
    }

    *out_pkt = av_packet_alloc();
    ret = avcodec_receive_packet(_enc_ctx, *out_pkt);

    if (ret == AVERROR(EAGAIN)) {
        av_packet_free(out_pkt);
        *out_pkt = nullptr;
        return AVERROR(EAGAIN);
    }

    if (!check_ret(ret, "接收编码数据失败")) {
        av_packet_free(out_pkt);
        *out_pkt = nullptr;
        return ret;
    }

    encodeTimer.end("encode frame by mpp");
    return 0;
}

int FFmpegUtils::flush_encoder(std::vector<AVPacket*>& out_pkts) {
    std::lock_guard<std::mutex> lock(_enc_mutex);
    if (!_enc_ctx) return -1;

    avcodec_send_frame(_enc_ctx, nullptr);
    while (true) {
        AVPacket* pkt = av_packet_alloc();
        int ret = avcodec_receive_packet(_enc_ctx, pkt);
        if (ret == AVERROR_EOF) {
            av_packet_free(&pkt);
            break;
        }
        if (ret == AVERROR(EAGAIN)) {
            av_packet_free(&pkt);
            continue;
        }
        if (ret < 0) {
            av_packet_free(&pkt);
            break;
        }
        out_pkts.push_back(pkt);
    }
    return 0;
}

void FFmpegUtils::release_encoder() {
    std::lock_guard<std::mutex> lock(_enc_mutex);
    if (_enc_frame) av_frame_free(&_enc_frame);
    if (_enc_ctx) avcodec_free_context(&_enc_ctx);
    _encoder = nullptr;
    std::cout << "编码模块已释放" << std::endl;
}


int FFmpegUtils::init_decoder(CodecType type) {
    std::lock_guard<std::mutex> lock(_dec_mutex);
    if (_dec_ctx) return -1;

    const std::string name = codec_to_string(type);
    _decoder = avcodec_find_decoder_by_name(name.c_str());
    if (!_decoder) return -1;

    _dec_ctx = avcodec_alloc_context3(_decoder);
    int ret = avcodec_open2(_dec_ctx, _decoder, nullptr);
    if (!check_ret(ret, "打开解码器失败")) goto fail;

    _dec_frame = av_frame_alloc();
    std::cout << "解码器初始化成功: " << name << std::endl;
    return 0;

fail:
    release_decoder();
    return -1;
}

int FFmpegUtils::decode(AVPacket* pkt, std::vector<uint8_t>& out_nv12) {
    std::lock_guard<std::mutex> lock(_dec_mutex);
    if (!_dec_ctx || !pkt) return -1;

    int ret = avcodec_send_packet(_dec_ctx, pkt);
    if (ret < 0 && ret != AVERROR(EAGAIN)) return ret;

    while (avcodec_receive_frame(_dec_ctx, _dec_frame) == 0) {
        if (_dec_frame->format != AV_PIX_FMT_NV12) continue;

        const int y_size = _dec_frame->width * _dec_frame->height;
        out_nv12.resize(y_size + y_size / 2);
        memcpy(out_nv12.data(), _dec_frame->data[0], y_size);
        memcpy(out_nv12.data() + y_size, _dec_frame->data[1], y_size / 2);
    }
    return 0;
}

void FFmpegUtils::release_decoder() {
    std::lock_guard<std::mutex> lock(_dec_mutex);
    if (_dec_frame) av_frame_free(&_dec_frame);
    if (_dec_ctx) avcodec_free_context(&_dec_ctx);
    _decoder = nullptr;
    std::cout << "解码模块已释放" << std::endl;
}

int FFmpegUtils::init(int width, int height) {
    if (init_capture(width, height) != 0) return -1;

    CodecParams params;
    params.width = width;
    params.height = height;
    params.pixfmt = AV_PIX_FMT_NV12;
    params.codec_type = CodecType::MJPEG_RKMPP;
    return init_encoder(params);
}

void FFmpegUtils::release_all() {
    release_capture();
    release_encoder();
    release_decoder();
}