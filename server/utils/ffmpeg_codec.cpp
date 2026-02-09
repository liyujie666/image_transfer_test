#include "ffmpeg_codec.h"
#include "logger.h"
#include "timer_util.h"
#include <cstring>

FFmpegCodec::FFmpegCodec(){
    av_log_set_level(AV_LOG_DEBUG);
}
FFmpegCodec::~FFmpegCodec(){
    releaseEncoder();
    releaseDecoder();
}

bool FFmpegCodec::initEncoder(const CodecParams& codecParams){
    std::lock_guard<std::mutex> locker(m_encode_mutex);
    if(m_is_enc_inited){
        LOG_ERROR("编码器已初始化");
        return false;
    }
    // 查找编码器
    m_codec_params = codecParams;
    switch (m_codec_params.codec_type)
    {
        case CodecType::H264_RKMPP: m_encoder = avcodec_find_encoder_by_name("h264_rkmpp");break;
        case CodecType::HEVC_RKMPP: m_encoder = avcodec_find_encoder_by_name("hevc_rkmpp");break;
        case CodecType::MJPEG_RKMPP: m_encoder = avcodec_find_encoder_by_name("mjpeg_rkmpp");break;
        default:
            LOG_ERROR("不支持此类型编码");
            break;
    }   

    if (!m_encoder) {
        LOG_ERROR("找不到编码器（%s）", codecTypeToString(m_codec_params.codec_type));
        return false;
    }

    // 分配编码器上下文
    m_encode_ctx = avcodec_alloc_context3(m_encoder);
    if(!m_encode_ctx){
        LOG_ERROR("Encoder context alloc failed");
        return false;
    }

    // 设置编码器参数
    m_encode_ctx->width = m_codec_params.width;
    m_encode_ctx->height = m_codec_params.height;
    m_encode_ctx->pix_fmt = m_codec_params.pixfmt;
    m_encode_ctx->bit_rate = m_codec_params.bitrate * 1000;
    m_encode_ctx->codec_type = AVMEDIA_TYPE_VIDEO;


    if (m_codec_params.codec_type == CodecType::MJPEG_RKMPP)
    {
        m_encode_ctx->gop_size = 1;      
        m_encode_ctx->max_b_frames = 0;     
        m_encode_ctx->framerate = {0,1};
        m_encode_ctx->time_base = {1,1};
        av_opt_set(m_encode_ctx->priv_data, "qp", "30", 0); 
    }
    else
    {
        // H264/HEVC 
        m_encode_ctx->framerate = {m_codec_params.fps,1};
        m_encode_ctx->time_base = {1,m_codec_params.fps};
        m_encode_ctx->gop_size = 120;
        m_encode_ctx->max_b_frames = 0;
        av_opt_set(m_encode_ctx->priv_data, "preset", "fast", 0);
        av_opt_set(m_encode_ctx->priv_data, "tune", "zerolatency", 0);
    }


    // 打开编码器
    int ret = avcodec_open2(m_encode_ctx,m_encoder,nullptr);
    if(!checkRet(ret,"编码器打开失败")){
        avcodec_free_context(&m_encode_ctx);
        return false;
    }

    // 创建编码器输入帧
    m_enc_frame = av_frame_alloc();
    m_enc_frame->width = m_encode_ctx->width;
    m_enc_frame->height = m_encode_ctx->height;
    m_enc_frame->format = m_encode_ctx->pix_fmt;
    ret = av_frame_get_buffer(m_enc_frame, 0);
    if (!checkRet(ret, "分配编码帧缓冲区失败")) {
        av_frame_free(&m_enc_frame);
        avcodec_free_context(&m_encode_ctx);
        return false;
    }

    m_is_enc_inited = true;
    // LOG_INFO("FFmpeg编码器初始化成功：%s | %dx%d | %dFPS | %dkbps",
    //          codecTypeToString(m_codec_params.codec_type),
    //          m_codec_params.width, m_codec_params.height, m_codec_params.fps, m_codec_params.bitrate);
    return true;

}

bool FFmpegCodec::initDecoder(CodecType codecType){
    std::lock_guard<std::mutex> locker(m_decode_mutex);
    
    if(m_is_dec_inited){
        LOG_ERROR("编码器已初始化");
        return false;
    }
    // 查找解码器
    switch (codecType)
    {
        case CodecType::H264_RKMPP: m_decoder = avcodec_find_decoder_by_name("h264_rkmpp");break;
        case CodecType::HEVC_RKMPP: m_decoder = avcodec_find_decoder_by_name("hevc_rkmpp");break;
        case CodecType::MJPEG_RKMPP: m_decoder = avcodec_find_decoder_by_name("mjpeg_rkmpp");break;
        default:
            LOG_ERROR("不支持此类型解码");
            break;
    }   

    if (!m_decoder) {
        LOG_ERROR("找不到解码器（%s）", codecTypeToString(m_codec_params.codec_type));
        return false;
    }

    // 分配解码器上下文
    m_decode_ctx = avcodec_alloc_context3(m_decoder);
    if(!m_decode_ctx){
        LOG_ERROR("Decoder context alloc failed");
        return false;
    }

    // 打开编码器
    int ret = avcodec_open2(m_decode_ctx,m_decoder,nullptr);
    if(!checkRet(ret,"解码器打开失败")){
        avcodec_free_context(&m_decode_ctx);
        return false;
    }

    // 创建解码器输出帧
    m_dec_frame = av_frame_alloc();
    if (!m_dec_frame) {
        LOG_ERROR("创建解码帧失败");
        avcodec_free_context(&m_decode_ctx);
        return false;
    }

    m_is_dec_inited = true;
    LOG_INFO("FFmpeg解码器初始化成功：%s", codecTypeToString(m_codec_params.codec_type));
    return true;
}


bool FFmpegCodec::encode(const std::vector<uint8_t>& frame_nv12,AVPacket** outPkt){
    std::lock_guard<std::mutex> locker(m_encode_mutex);
    if(!m_is_enc_inited || frame_nv12.empty() || !outPkt){
        LOG_ERROR("编码器未初始化或输入无效");
        return false;
    }

    TimerUtil encodeTimer;
    encodeTimer.start();

    int width = m_encode_ctx->width;
    int height = m_encode_ctx->height;
    int y_size = width * height;
    int uv_size = width * height / 2;
    
    int expected_size = y_size + uv_size;
    // 查输入帧大小是否匹配
    if (frame_nv12.size() != expected_size) {
        LOG_ERROR("输入NV12帧大小不匹配（预期：%d，实际：%d）", expected_size, frame_nv12.size());
        return false;
    }
    // 填充编码帧数据（NV12）
    av_frame_make_writable(m_enc_frame);
    
    // nv12
    memcpy(m_enc_frame->data[0],frame_nv12.data(),y_size);              // Y
    memcpy(m_enc_frame->data[1],frame_nv12.data() + y_size,uv_size);    // UV 
    m_enc_frame->linesize[0] = width;
    m_enc_frame->linesize[1] = width;

    encodeTimer.end("copy NV12");

    encodeTimer.reset();
    // 发送帧到编码器
    int ret = avcodec_send_frame(m_encode_ctx,m_enc_frame);
    if (!checkRet(ret, "发送帧到编码器失败") && ret != AVERROR(EAGAIN)) {
        return false;
    }

    // 接收编码包
    *outPkt = av_packet_alloc();
    ret = avcodec_receive_packet(m_encode_ctx,*outPkt);
    if(ret == AVERROR(EAGAIN)){
        av_packet_free(outPkt);
        *outPkt = nullptr;
        return true;
    }

    if (!checkRet(ret, "接收编码数据包失败")) {
        av_packet_free(outPkt);
        *outPkt = nullptr;
        return false;
    }

    encodeTimer.end("mjpeg_rkmpp encode");
    LOG_DEBUG("编码成功：帧索引=%d，数据包大小=%d字节", m_enc_frame_idx - 1, (*outPkt)->size);
    return true;
}

bool FFmpegCodec::encode(const cv::Mat& frame_nv12, AVPacket** outPkt) {
    std::lock_guard<std::mutex> locker(m_encode_mutex);
    if (!m_is_enc_inited || frame_nv12.empty() || !outPkt) {
        LOG_ERROR("编码器未初始化或输入无效");
        return false;
    }

    TimerUtil encodeTimer;
    encodeTimer.start();

    int width = m_encode_ctx->width;
    int height = m_encode_ctx->height;
    int y_size = width * height;
    int uv_size = width * height / 2;

    int expected_size = y_size + uv_size;
    int actual_size   = frame_nv12.rows * frame_nv12.cols;

    if (actual_size != expected_size) {
        LOG_ERROR("NV12 size mismatch: expect=%d actual=%d (w=%d h=%d rows=%d cols=%d)",
                expected_size, actual_size,
                width, height,
                frame_nv12.rows, frame_nv12.cols);
        return false;
    }

    av_frame_make_writable(m_enc_frame);

    uint8_t* src = frame_nv12.data;

    memcpy(m_enc_frame->data[0], src, y_size);              // Y
    memcpy(m_enc_frame->data[1], src + y_size, uv_size);    // UV
    m_enc_frame->linesize[0] = width;
    m_enc_frame->linesize[1] = width;

    encodeTimer.end("copy frame");

    encodeTimer.reset();
    // 发送帧
    int ret = avcodec_send_frame(m_encode_ctx, m_enc_frame);
    if (!checkRet(ret, "发送帧到编码器失败") && ret != AVERROR(EAGAIN)) {
        return false;
    }

    // 接收编码包
    *outPkt = av_packet_alloc();
    ret = avcodec_receive_packet(m_encode_ctx, *outPkt);
    if (ret == AVERROR(EAGAIN)) {
        av_packet_free(outPkt);
        *outPkt = nullptr;
        return true;
    }

    if (!checkRet(ret, "接收编码数据包失败")) {
        av_packet_free(outPkt);
        *outPkt = nullptr;
        return false;
    }
    encodeTimer.end("encode frame");
    //LOG_DEBUG("编码成功：帧索引=%d，数据包大小=%d字节", m_enc_frame_idx - 1, (*outPkt)->size);
    return true;
}

bool FFmpegCodec::decode(AVPacket* pkt,std::vector<uint8_t>& frame_nv12){
    std::lock_guard<std::mutex> locker(m_decode_mutex);
    if(!m_is_dec_inited || !pkt || pkt->size <=0){
        LOG_ERROR("解码器未初始化或输入无效");
        return false;
    }

    int ret = avcodec_send_packet(m_decode_ctx,pkt);
    if (ret != AVERROR(EAGAIN) && !checkRet(ret, "发送数据包到解码器失败")) {
        return false;
    }

    while(true){
        ret = avcodec_receive_frame(m_decode_ctx, m_dec_frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        } else if (ret < 0) {
            checkRet(ret, "解码器接收帧失败");
            return false;
        }

        if(m_dec_frame->format != AV_PIX_FMT_NV12){
            LOG_ERROR("解码输出格式不匹配（预期NV12，实际：%d）", m_dec_frame->format);
            continue; 
        }

        int y_size = m_dec_frame->width * m_dec_frame->height;
        int uv_size = y_size / 2;
        int total_size = y_size + uv_size;

        frame_nv12.resize(total_size);
        memcpy(frame_nv12.data(),m_dec_frame->data[0],y_size);
        memcpy(frame_nv12.data() + y_size,m_dec_frame->data[1],uv_size);

    }
    
    return true;

}

bool FFmpegCodec::flushEncoder(std::vector<AVPacket*>& outPkts){
    std::lock_guard<std::mutex> locker(m_encode_mutex);
    if (!m_is_enc_inited) {
        LOG_ERROR("编码器未初始化，无法刷新");
        return false;
    }
    int ret = 0;

    ret = avcodec_send_frame(m_encode_ctx, nullptr);
    if (ret != AVERROR_EOF && !checkRet(ret, "刷新编码器发送失败")) {
        return false;
    }

    while (true) {
        AVPacket* pkt = av_packet_alloc();
        ret = avcodec_receive_packet(m_encode_ctx, pkt);

        if (ret == AVERROR_EOF) {
            av_packet_free(&pkt);
            break;
        } else if (ret == AVERROR(EAGAIN)) {
            av_packet_free(&pkt);
            continue;
        } else if (!checkRet(ret, "接收刷新帧失败")) {
            av_packet_free(&pkt);
            return false;
        }

        // 收集有效编码包
        outPkts.push_back(pkt);
        LOG_DEBUG("刷新编码器：输出缓存帧，包大小=%d字节", pkt->size);
    }

    LOG_INFO("编码器刷新完成，共输出%d个缓存帧", outPkts.size());
    return true;
}
void FFmpegCodec::releaseEncoder(){
    std::lock_guard<std::mutex> locker(m_encode_mutex);
    if(!m_is_enc_inited) return;

    // 释放硬件编码上下文
    if(m_encode_ctx && m_encode_ctx->hw_device_ctx){
        av_buffer_unref(&m_encode_ctx->hw_device_ctx);
    }

    // 释放编码帧
    if(m_enc_frame){
        av_frame_free(&m_enc_frame);
        m_enc_frame = nullptr;
    }

    // 释放编码器上下文
    if(m_encode_ctx){
        avcodec_free_context(&m_encode_ctx);
        m_encode_ctx = nullptr;
    }

    m_encoder = nullptr;
    m_is_enc_inited = false;
    m_enc_frame_idx = 0;

    LOG_INFO("FFmpeg编码器资源释放成功");

}
void FFmpegCodec::releaseDecoder(){
    std::lock_guard<std::mutex> locker(m_decode_mutex);
    if(!m_is_dec_inited) return;

    // 释放编码帧
    if(m_dec_frame){
        av_frame_free(&m_dec_frame);
        m_dec_frame = nullptr;
    }

    // 释放编码器上下文
    if(m_decode_ctx){
        avcodec_free_context(&m_decode_ctx);
        m_decode_ctx = nullptr;
    }

    m_decoder = nullptr;
    m_is_dec_inited = false;

    LOG_INFO("FFmpeg解码器资源释放成功");
}

CodecParams FFmpegCodec::getCodecParams() const{
    return m_codec_params;
}

bool FFmpegCodec::checkRet(int ret, const std::string& err_msg){
    if(ret >= 0) return true;
    char err_buf[1024] = {0};
    av_strerror(ret,err_buf,sizeof(err_buf));
    LOG_ERROR("%s : %s (错误码：%d)",err_msg.c_str(),err_buf,ret);
    return false; 
}

std::string FFmpegCodec::codecTypeToString(const CodecType& type)
{
    switch (type)
    {
        case CodecType::H264_RKMPP:
            return "H264_RKMPP";
        case CodecType::HEVC_RKMPP:
            return "HEVC_RKMPP";
        case CodecType::MJPEG_RKMPP:
            return "MJPEG_RKMPP";
        default:
            return "Unknown";
    }
}
