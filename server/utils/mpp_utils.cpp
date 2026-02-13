#include "mpp_utils.h"
#include "logger.h"

namespace mpp{
MppEncoder::MppEncoder()
    : mpp_ctx_(nullptr), 
      mpp_mpi_(nullptr),
      mpp_buf_grp_(nullptr),
      mpp_frame_(nullptr), 
      codec_type_(MppCodecType::H264),
      width_(0), height_(0), fps_(0)
{
    timer_ = std::make_unique<TimerUtil>();
}

MppEncoder::~MppEncoder() 
{ 
    deinit(); 
}

MPP_RET MppEncoder::configureEncoder() {
    MPP_RET ret;
    MppEncCfg cfg;
    mpp_enc_cfg_init(&cfg);
    // 基础参数
    ret = mpp_mpi_->control(mpp_ctx_, MPP_ENC_GET_CFG, cfg);
    if (ret != MPP_OK) {
        LOG_ERROR("encoder config get failed! ret: %d", ret);
        return ret;
    }
    // 图像参数
    mpp_enc_cfg_set_s32(cfg, "prep:width", width_);
    mpp_enc_cfg_set_s32(cfg, "prep:height", height_);
    mpp_enc_cfg_set_s32(cfg, "prep:hor_stride", align_width_);
    mpp_enc_cfg_set_s32(cfg, "prep:ver_stride", align_height_);
    mpp_enc_cfg_set_s32(cfg, "prep:format", MPP_FMT_YUV420SP);  // NV12

    // 帧率控制
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_flex", 0);     
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_num", fps_);  
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_denom", 1);  
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_flex", 0);   
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_num", fps_);  
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_denom", 1); 

    if(codec_type_ == MppCodecType::H264){
        // 码率控制
        int bps = width_ * height_ * fps_ * 0.1;                   // 基础码率估算
        mpp_enc_cfg_set_s32(cfg, "rc:mode", MPP_ENC_RC_MODE_CBR);  // CBR: 恒定码率‌
        mpp_enc_cfg_set_s32(cfg, "rc:bps_target", bps);
        mpp_enc_cfg_set_s32(cfg, "rc:bps_max", bps * 3 / 2);
        mpp_enc_cfg_set_s32(cfg, "rc:bps_min", bps / 2);
         
        // H264高级参数配置
        mpp_enc_cfg_set_s32(cfg, "codec:type", MPP_VIDEO_CodingAVC);
        mpp_enc_cfg_set_s32(cfg, "rc:qp_init", 26);
        mpp_enc_cfg_set_s32(cfg, "rc:qp_max", 36);          // 值越高运动画面失真风险越大‌
        mpp_enc_cfg_set_s32(cfg, "rc:qp_min", 20);          // 值越低静止画面越清晰，但码率开销越大‌
        mpp_enc_cfg_set_s32(cfg, "rc:qp_step", 4);
        mpp_enc_cfg_set_s32(cfg, "h264:profile", 100);      // High profile
        mpp_enc_cfg_set_s32(cfg, "h264:level", 40);         // 1080p@30fps
        mpp_enc_cfg_set_s32(cfg, "h264:cabac_en", 1);
        mpp_enc_cfg_set_s32(cfg, "h264:cabac_idc", 0);
        mpp_enc_cfg_set_s32(cfg, "h264:trans8x8", 1);
        mpp_enc_cfg_set_s32(cfg, "rc:gop", fps_);           // 关键帧间隔，1秒GOP
    }else{
        // JPEG 
        mpp_enc_cfg_set_s32(cfg, "codec:type", MPP_VIDEO_CodingMJPEG);
        mpp_enc_cfg_set_s32(cfg, "rc:mode", MPP_ENC_RC_MODE_FIXQP);
        
    }

    // 应用配置
    ret = mpp_mpi_->control(mpp_ctx_, MPP_ENC_SET_CFG, cfg);
    if (ret != MPP_OK) {
        LOG_ERROR("encoder config set failed! ret: %d", ret);
        return ret;
    }
    mpp_mpi_->reset(mpp_ctx_);  // 强制重新初始化编码器
    mpp_enc_cfg_deinit(cfg);

    return MPP_OK;
}


void MppEncoder::setEncodeCallback(EncodeFrameCallback callback){
    encode_callback_ = std::move(callback);
}
int MppEncoder::init(int width, int height, int fps, MppCodecType codec_type){
    width_ = width;
    height_ = height;
    fps_ = fps;
    codec_type_ = codec_type;
    align_width_ = align16(width);
    align_height_ = align16(height);
    LOG_INFO("[MppEncoder] [init] width: %d ,height: %d,fps: %d, codec: %s",
             width, height, fps, 
             (codec_type == MppCodecType::H264 ? "H264" : "JPEG"));
    LOG_INFO("[MppEncoder] [init] align_width: %d ,align_height: %d",align_width_,align_height_);

    // 创建mpp上下文
    MPP_RET ret = MPP_OK;
    ret = mpp_create(&mpp_ctx_,&mpp_mpi_);
    if(ret != MPP_OK){
        LOG_ERROR("mpp_create failed! ret: %d", ret);
        return -1;
    }

    // 初始化编码器
    MppCodingType mpp_codec = (codec_type == MppCodecType::H264) ? MPP_VIDEO_CodingAVC : MPP_VIDEO_CodingMJPEG;
    ret = mpp_init(mpp_ctx_,MPP_CTX_ENC,mpp_codec);
    if(ret != MPP_OK){
        LOG_ERROR("mpp_init failed! ret: %d", ret);
        return -1;
    }

    // 配置编码器参数
    ret = configureEncoder();
    if(ret != MPP_OK){
        LOG_ERROR("configureEncoder failed! ret: %d", ret);
        return -1;
    }

    // 创建内存组
    ret = mpp_buffer_group_get_internal(&mpp_buf_grp_,MPP_BUFFER_TYPE_DRM);
    if(ret != MPP_OK){
        LOG_ERROR("buffer group creation failed! ret: %d", ret);
        return -1;
    }

    mpp_frame_init(&mpp_frame_);

    return 0;
}
void MppEncoder::encode(unsigned char *src_data, int frame_count){
    if (!src_data) return;
    if (!mpp_ctx_ || !mpp_mpi_ || !mpp_buf_grp_) {
        return;
    }

    MppBuffer buffer;
    size_t src_size = width_ * height_;
    size_t align_size = align_width_ * align_height_;

    MPP_RET ret = mpp_buffer_get(mpp_buf_grp_,&buffer,align_size + align_size / 2);
    if (ret != MPP_OK) {
        LOG_ERROR("failed to get buffer! ret: %d", ret);
        return;
    }

    // 数据拷贝
    unsigned char* dst_data = (unsigned char*)mpp_buffer_get_ptr(buffer);
    memcpy(dst_data, src_data, src_size);                                                              // Y
    memcpy(dst_data + align_size, src_data + src_size, src_size / 2);                                  // UV

    // 构建帧对象
    mpp_frame_set_buffer(mpp_frame_, buffer);
    mpp_frame_set_width(mpp_frame_, width_);
    mpp_frame_set_height(mpp_frame_, height_);
    mpp_frame_set_hor_stride(mpp_frame_, align_width_);
    mpp_frame_set_ver_stride(mpp_frame_, align_height_);
    mpp_frame_set_fmt(mpp_frame_, MPP_FMT_YUV420SP);
    int64_t pts_counter = (int64_t)frame_count * (90000 / fps_);
    mpp_frame_set_pts(mpp_frame_, pts_counter);
    mpp_frame_set_eos(mpp_frame_, 0);

    // 提交编码
    ret = mpp_mpi_->encode_put_frame(mpp_ctx_, mpp_frame_);
    if (ret != MPP_OK) {
        LOG_ERROR("encode_put_frame failed! ret: %d, codec: %s", 
                  ret, (codec_type_ == MppCodecType::H264 ? "H264" : "JPEG"));
        mpp_buffer_put(buffer);
        return;
    }

    // 获取输出包
    MppPacket pkt;
    mpp_packet_init(&pkt, nullptr, 0);
    ret = mpp_mpi_->encode_get_packet(mpp_ctx_, &pkt);
    if (ret == MPP_OK && pkt) {
        unsigned char* data = (unsigned char*)mpp_packet_get_data(pkt);
        size_t size = mpp_packet_get_length(pkt);
        
        bool is_intra = false;
        if (codec_type_ == MppCodecType::H264) {
            MppMeta meta = mpp_packet_get_meta(pkt);
            RK_S32 intra = 0;
            mpp_meta_get_s32(meta, KEY_OUTPUT_INTRA, &intra);
            is_intra = (intra > 0);
        }
        
        // 调用回调函数
        if (encode_callback_) {
            encode_callback_(data, size, frame_count, is_intra);
        }
    } else {
        LOG_ERROR("encode_get_mpp_packet_ failed! ret: %d, codec: %s", 
                  ret, (codec_type_ == MppCodecType::H264 ? "H264" : "JPEG"));
    }

    mpp_packet_deinit(&pkt);
    mpp_buffer_put(buffer);
}

// 兼容：DMA模式编码（支持H264/JPEG）
void MppEncoder::encode_dma(int fd, int frame_count) {
    if (fd < 0 || !mpp_ctx_ || !mpp_mpi_) {
        LOG_ERROR("DMA编码参数无效（fd=%d）或编码器未初始化", fd);
        return;
    }

    size_t y_size = (size_t)width_ * height_;
    size_t uv_size = y_size / 2;
    size_t total_size = y_size + uv_size;

    MppBuffer dma_buffer = nullptr;
    MPP_RET ret = MPP_OK;

    timer_->start();
    // 导入V4L2的DMABuf fd（缓存复用）
    auto it = dma_buf_cache_.find(fd);
    if (it != dma_buf_cache_.end()) {
        dma_buffer = it->second;
    } else {
        MppBufferInfo info = {};
        info.type = MPP_BUFFER_TYPE_DRM; 
        info.fd = fd; 
        info.size = total_size;  
        ret = mpp_buffer_import(&dma_buffer, &info);

        if (ret != MPP_OK) {
            LOG_ERROR("导入DMABuf fd失败! fd=%d, ret=%d, 总大小=%zu", fd, ret, total_size);
            return;
        }
        dma_buf_cache_[fd] = dma_buffer;
        LOG_INFO("DMA编码：导入fd=%d成功（总大小=%zu），codec: %s", 
                 fd, total_size, (codec_type_ == MppCodecType::H264 ? "H264" : "JPEG"));
    }

    // 绑定DMABuf到MPP Frame
    mpp_frame_set_buffer(mpp_frame_, dma_buffer);
    mpp_frame_set_width(mpp_frame_, width_);          
    mpp_frame_set_height(mpp_frame_, height_);      
    mpp_frame_set_hor_stride(mpp_frame_, align_width_); 
    mpp_frame_set_ver_stride(mpp_frame_, align_height_);
    mpp_frame_set_fmt(mpp_frame_, MPP_FMT_YUV420SP);     // NV12
    mpp_frame_set_pts(mpp_frame_, (int64_t)frame_count * (90000 / fps_)); // PTS
    mpp_frame_set_eos(mpp_frame_, 0);

    // DMABuf帧编码
    ret = mpp_mpi_->encode_put_frame(mpp_ctx_, mpp_frame_);
    if (ret != MPP_OK) {
        LOG_ERROR("DMA提交帧失败! ret=%d, codec: %s", 
                  ret, (codec_type_ == MppCodecType::H264 ? "H264" : "JPEG"));
        return;
    }

    // 获取编码结果
    MppPacket pkt;
    mpp_packet_init(&pkt, nullptr, 0);
    ret = mpp_mpi_->encode_get_packet(mpp_ctx_, &pkt);
    if (ret == MPP_OK && pkt) {
        unsigned char* data = (unsigned char*)mpp_packet_get_data(pkt);
        size_t size = mpp_packet_get_length(pkt);
        
        bool is_intra = false;
        if (codec_type_ == MppCodecType::H264) {
            RK_S32 intra = 0;
            mpp_meta_get_s32(mpp_packet_get_meta(pkt), KEY_OUTPUT_INTRA, &intra);
            is_intra = (intra > 0);
        }

        if (encode_callback_) {
            encode_callback_(data, size, frame_count, is_intra);
        }
    } else {
        LOG_ERROR("DMA获取编码包失败! ret=%d, codec: %s", 
                  ret, (codec_type_ == MppCodecType::H264 ? "H264" : "JPEG"));
    }

    timer_->end((std::string("[Encode] [MPP_DMA_") + (codec_type_ == MppCodecType::H264 ? "H264]" : "JPEG]")).c_str());
    mpp_packet_deinit(&pkt);
}

void MppEncoder::deinit() {
    if (mpp_frame_) mpp_frame_deinit(&mpp_frame_);
    if (mpp_mpi_) mpp_mpi_->reset(mpp_ctx_);
    if (mpp_ctx_) mpp_destroy(mpp_ctx_);
    if (mpp_buf_grp_) mpp_buffer_group_put(mpp_buf_grp_);

    // 释放DMA缓存
    for (auto& pair : dma_buf_cache_) {
        if (pair.second) mpp_buffer_put(pair.second);
    }
    dma_buf_cache_.clear();

    width_ = 0;
    height_ = 0;
    align_width_ = 0;
    align_height_ = 0;
    fps_ = 0;
    codec_type_ = MppCodecType::H264;

    mpp_ctx_ = nullptr;
    mpp_mpi_ = nullptr;
    mpp_buf_grp_ = nullptr;
    LOG_INFO("[MppEncoder] 资源已释放");
}
};
