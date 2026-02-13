#include "capture_controller.h"
#include "timer_util.h"
#include "image_utils.h"
#include "logger.h"
#include <unistd.h>

CaptureController::CaptureController(const std::string& dev_node)
    : dev_node_(dev_node),is_multiple_capturing_(false)
{
}
CaptureController::~CaptureController()
{
    release();
}

int CaptureController::init(const CameraConfig& config){

    release();
    cur_config_ = config;

    if(init_devices() < 0) return -1;
    if(init_encoders() < 0) return -1;
    return 0;
    
}

int CaptureController::init_devices()
{
    switch (cur_config_.cap_device)
    {
    case CaptureDevice::V4L2:
        v4l2_cap_ = std::make_unique<V4L2Capture>(dev_node_,cur_config_.width,cur_config_.height);
        if(!v4l2_cap_->init()) return -1;
        break;
    case CaptureDevice::V4L2_DMA:
        v4l2_cap_ = std::make_unique<V4L2Capture>(dev_node_,cur_config_.width,cur_config_.height,true);
        if(!v4l2_cap_->init()) return -1;
        break;
    case CaptureDevice::OPENCV:
        opencv_cap_ = std::make_unique<cv::VideoCapture>(dev_node_,cv::CAP_V4L2);
        if(!opencv_cap_->isOpened()){
            LOG_ERROR("Failed to open device by opencv: %s",dev_node_);
            return -1;
        }
        opencv_cap_->set(cv::CAP_PROP_FRAME_WIDTH, cur_config_.width);
        opencv_cap_->set(cv::CAP_PROP_FRAME_HEIGHT, cur_config_.height);
        opencv_cap_->set(cv::CAP_PROP_FPS, cur_config_.fps);
        opencv_cap_->set(cv::CAP_PROP_BUFFERSIZE, 4);
        break;
    case CaptureDevice::FFMPEG:
        ffmpeg_cap_ = std::make_unique<FFmpegUtils>(dev_node_);
        if(ffmpeg_cap_->init_capture(cur_config_.width,cur_config_.height) < 0) return -1;
        break;
    default:
        LOG_ERROR("Unsupported capture device");
        break;
    }

    return 0;
}

int CaptureController::init_encoders(){
    int ret;
    switch (cur_config_.encode_type)
    {
    case EncodeType::FFMPEG:{
        ffmpeg_enc_ = std::make_unique<FFmpegUtils>(dev_node_);

        CodecParams params;
        params.width = cur_config_.width;
        params.height = cur_config_.height; 
        params.pixfmt = AV_PIX_FMT_NV12;      
        params.codec_type = CodecType::MJPEG_RKMPP;
        if(ffmpeg_enc_->init_encoder(params) < 0) return -1;
        break;
    }

    case EncodeType::MPP_DMA:{
        mpp_enc_ = std::make_unique<mpp::MppEncoder>();
        if(mpp_enc_->init(cur_config_.width,cur_config_.height,cur_config_.fps,mpp::MppCodecType::JPEG) < 0) return -1;
        
        mpp_enc_->setEncodeCallback([this](const uint8_t* data, size_t size, int frame_count, bool is_key){
            std::lock_guard<std::mutex> lock(encode_mutex_);
            encode_tmp_data_.clear();
            encode_tmp_data_.assign(data, data + size);
        });
        frame_counter_ = 0;
        break;
    }
    case EncodeType::OPENCV:{
        break;
    }
    default:
        LOG_ERROR("Unsupported encode type");
        break;
    }

    return 0;
}


int CaptureController::capture_frame_single(std::vector<uint8_t>& dst_frame, ImageMeta& meta)
{
    cv::Mat src_frame;
    int dma_fd = -1;

    // 采集
    switch (cur_config_.cap_device)
    {
    case CaptureDevice::V4L2:
        if(capture_single_by_v4l2(src_frame) < 0) return -1;break;
    case CaptureDevice::V4L2_DMA:
        if(capture_single_by_v4l2_dma(dma_fd) < 0) return -1;break;
    case CaptureDevice::OPENCV:
        if(capture_single_by_opencv(src_frame) < 0) return -1;break;
    case CaptureDevice::FFMPEG:
        if(capture_single_by_ffmpeg(src_frame) < 0) return -1;break;
    default:
        LOG_ERROR("Unsupported capture device");
        break;
    }
    //LOG_INFO("capture frame success");

    // 编码
    switch (cur_config_.encode_type)
    {
    case EncodeType::FFMPEG:
        if(encode_by_ffmpeg(src_frame,dst_frame,meta) < 0) return -1;break;
    case EncodeType::MPP_DMA:
        if(encode_by_mpp_dma(dma_fd,dst_frame,meta) < 0) return -1;break;
    case EncodeType::OPENCV:
        if(encode_by_opencv(src_frame,dst_frame,meta) < 0) return -1;break;
    default:
        LOG_ERROR("Unsupported encode type");
        break;
    }
    //LOG_INFO("encode frame success");

    return 0;

}
int CaptureController::start_capture_multiple()
{
    if(is_multiple_capturing_){
        LOG_WARN("multiple capture already running");
        return 0;
    }

    if (cur_config_.width == 0 || cur_config_.height == 0) {
        LOG_ERROR("CaptureController not initialized");
        return -1;
    }

    if (pub_stream_id_ == 0) {
        LOG_ERROR("RPC pub stream ID not set");
        return -1;
    }

    is_multiple_capturing_ = true;
    capture_multiple_thread_ = std::thread(&CaptureController::capture_multiple_loop,this);
    LOG_INFO("Continuous capture started (dev: %s, %dx%d, fps: %d)", 
             dev_node_.c_str(), cur_config_.width, cur_config_.height, cur_config_.fps);
    return 0;
}

void CaptureController::stop_capture_multiple()
{
    if (!is_multiple_capturing_) {
        return;
    }

    is_multiple_capturing_ = false;
    if (capture_multiple_thread_.joinable()) {
        capture_multiple_thread_.join();
    }
    LOG_INFO("Continuous capture thread stopped (dev: %s)", dev_node_.c_str());
}

int CaptureController::start_rtsp_pusher(const CameraConfig& config){
    std::lock_guard<std::mutex> lock(push_mutex_);
    
    if (is_pushing_) {
        LOG_WARN("RTSP推流已在运行");
        return 0;
    }
    cur_config_ = config;
    ffmpeg_pusher_ = std::make_unique<FFmpegUtils>(dev_node_);
    if(ffmpeg_pusher_->init(cur_config_.width,cur_config_.height) < 0) return -1;

    is_pushing_ = true;
    
    push_thread_ = std::thread(&FFmpegUtils::start_capture_encode_push,ffmpeg_pusher_.get(),RTSP_URL);

    LOG_INFO("RTSP推流已启动，地址：%s", RTSP_URL.c_str());
    return 0;
}
void CaptureController::stop_rtsp_pusher(){
    std::lock_guard<std::mutex> lock(push_mutex_);
    
    if (!is_pushing_) {
        return;
    }

    ffmpeg_pusher_->stop_capture_encode_push();
    
    if (push_thread_.joinable()) {
        push_thread_.join();
    }
    
    // 4. 更新状态
    is_pushing_ = false;
    LOG_INFO("RTSP推流已停止，地址：%s", RTSP_URL.c_str());
}



void CaptureController::release()
{
    stop_capture_multiple();

    if(v4l2_cap_) {
        v4l2_cap_->close();
        v4l2_cap_.reset(); 
    }
    if(opencv_cap_) {
        opencv_cap_->release();  
        opencv_cap_.reset();
    }
    if(ffmpeg_cap_) {
        ffmpeg_cap_->release_all();
        ffmpeg_cap_.reset();
    }
    if(ffmpeg_enc_) {
        ffmpeg_enc_->release_all();
        ffmpeg_enc_.reset();
    }
    if(ffmpeg_pusher_){
        ffmpeg_pusher_->release_all();
        ffmpeg_pusher_.reset();
    }
    
}


int CaptureController::capture_single_by_opencv(cv::Mat& src_frame){
    if(!opencv_cap_) return -1;
    TimerUtil timer;
    timer.start();
    *opencv_cap_ >> src_frame;
    if(src_frame.empty()){
        LOG_ERROR("Failed to capture frame by opencv");
        return -1;
    }
    timer.end("[Capture] [OpenCV]");
    return 0;
}
int CaptureController::capture_single_by_v4l2(cv::Mat& src_frame){
    if(!v4l2_cap_) return -1;

    if(!v4l2_cap_->captureFrame(src_frame)){
        LOG_ERROR("Failed to capture frame by V4L2");
        return -1;
    }

    return 0;
}

int CaptureController::capture_single_by_v4l2_dma(int& dma_fd){
    if(!v4l2_cap_) return -1;

    if(!v4l2_cap_->captureFrame(dma_fd)){
        LOG_ERROR("Failed to capture frame by V4L2_DMA");
        return -1;
    }

    return 0;
}
int CaptureController::capture_single_by_ffmpeg(cv::Mat& src_frame){
    if(!ffmpeg_cap_) return -1;

    if(ffmpeg_cap_->capture_frame(src_frame) < 0){
        LOG_ERROR("Failed to capture frame by FFmpeg");
        return -1;
    }

    return 0;
}


int CaptureController::encode_by_ffmpeg(const cv::Mat& src_frame,std::vector<uint8_t>& dst_frame, ImageMeta& meta){
    if(!ffmpeg_enc_) return -1;
    const int max_retry = 5;        
    const int retry_delay_us = 5000;
    int retry_cnt = 0;
    AVPacket* pkt = nullptr;
    int ret = 0;

    // 若用opencv采集需转换格式，bgr-->nv12
    if(cur_config_.cap_device == CaptureDevice::OPENCV){
        cv::Mat nv12_frame;
        if(convertBgrToNV12ByRga(src_frame,nv12_frame) < 0){
            return -1;
        }

        while (retry_cnt < max_retry) {

            ret = ffmpeg_enc_->encode(nv12_frame, &pkt);
        
            if (ret == 0 && pkt != nullptr) {
                break;
            }
        
            if (ret == AVERROR(EAGAIN)) {
                retry_cnt++;
                usleep(retry_delay_us);
                LOG_WARN("编码器忙，重试 %d/%d",retry_cnt,max_retry);
                continue;
            }

            LOG_ERROR("帧编码错误，错误码：%d",ret);
            break;
        }

    }else{

        while (retry_cnt < max_retry) {

            ret = ffmpeg_enc_->encode(src_frame, &pkt);
        
            if (ret == 0 && pkt != nullptr) {
                break;
            }
        
            if (ret == AVERROR(EAGAIN)) {
                retry_cnt++;
                usleep(retry_delay_us);
                LOG_WARN("编码器忙，重试 %d/%d",retry_cnt,max_retry);
                continue;
            }

            LOG_ERROR("帧编码错误，错误码：%d",ret);
            break;
        }
    }

    if (pkt && pkt->size > 0) {
        dst_frame.resize(pkt->size);
        memcpy(dst_frame.data(), pkt->data, pkt->size);
        //LOG_INFO("pkt size: %d",pkt->size);
        meta.cols = src_frame.cols;
        meta.rows = src_frame.rows * 2 / 3;
        meta.comp_data_len = pkt->size;
        av_packet_free(&pkt);
        return 0;
    }

    return -1;

}

int CaptureController::encode_by_mpp_dma(int& dma_fd, std::vector<uint8_t>& dst_frame, ImageMeta& meta){
    if (!mpp_enc_ || dma_fd < 0) {
        LOG_ERROR("MPP编码器未初始化或DMA fd无效（fd=%d）", dma_fd);
        return -1;
    }

    {
        std::lock_guard<std::mutex> lock(encode_mutex_);
        encode_tmp_data_.clear();
    }

    mpp_enc_->encode_dma(dma_fd,++frame_counter_);

    int retry = 0;
    const int max_retry = 10;
    const int retry_delay_us = 1000; // 1ms
    while (retry < max_retry) {
        std::lock_guard<std::mutex> lock(encode_mutex_);
        if (!encode_tmp_data_.empty()) {
            break;
        }
        retry++;
        usleep(retry_delay_us);
    }

    {
        std::lock_guard<std::mutex> lock(encode_mutex_);
        if (encode_tmp_data_.empty()) {
            LOG_ERROR("MPP DMA编码失败，未获取到编码数据（fd=%d，帧%d）", dma_fd, frame_counter_);
            return -1;
        }

        // 填充输出数据和元信息
        dst_frame = std::move(encode_tmp_data_);
        meta.cols = cur_config_.width;
        meta.rows = cur_config_.height;
        meta.comp_data_len = dst_frame.size();
        meta.is_compressed = true;
    }

    LOG_DEBUG("MPP DMA编码成功：帧%d，大小%d字节", frame_counter_, dst_frame.size());
    return 0;
}

int CaptureController::encode_by_opencv(const cv::Mat& src_frame,std::vector<uint8_t>& dst_frame, ImageMeta& meta){
    cv::Mat encode_img = src_frame.clone();
    

    TimerUtil timer;
    timer.start();
    
    bool ret;
    // 非opencv采集的视频帧为nv12格式，需转换为bgr
    if(cur_config_.cap_device != CaptureDevice::OPENCV){
        cv::Mat bgr_frame;
        if(convertNv12ToBgrByRga(src_frame,bgr_frame) < 0) {
            return -1;
        }
        ret = cv::imencode(".jpg", bgr_frame, dst_frame, encode_params_opencv);
    }else{
        ret = cv::imencode(".jpg", src_frame, dst_frame, encode_params_opencv);
    }

    if(ret) {
        meta.is_compressed = true;
        meta.compress_type = COMPRESS_FORMAT;
        meta.cols = src_frame.cols;
        meta.rows = src_frame.rows;
        meta.comp_data_len = dst_frame.size();
    }
    timer.end("[Encode] [OpenCV]");
    return ret;
}

void CaptureController::capture_multiple_loop()
{
    std::vector<uint8_t> frame_data;
    ImageMeta frame_meta;
    // 帧间隔
    int frame_interval_ms = 1000 / cur_config_.fps;

    while (is_multiple_capturing_) {
        std::lock_guard<std::mutex> lock(capture_mtx_);
        
        if (capture_frame_single(frame_data, frame_meta) != 0) {
            LOG_ERROR("Continuous capture single frame failed, retry...");
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        // 发布帧到RPC PUB
        if (!publish_frame(frame_data, frame_meta)) {
            LOG_WARN("Publish frame failed, continue...");
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(frame_interval_ms));
    }
}

bool CaptureController::publish_frame(const std::vector<uint8_t>& frame_data, const ImageMeta& meta)
{
    if (frame_data.empty() || pub_stream_id_ == 0) {
        return false;
    }

    try {
        RpcImageResponse img_resp;
        img_resp.meta = meta;
        img_resp.image_data = frame_data;

        msgpack::sbuffer buffer;
        msgpack::pack(buffer, img_resp);

        return hub::RpcEngine::getInstance().publish(
            pub_stream_id_, 
            buffer.data(), 
            buffer.size()
        );
    } catch (const std::exception& e) {
        LOG_ERROR("Publish frame error: %s", e.what());
        return false;
    }
}