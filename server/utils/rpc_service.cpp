#include "rpc_service.h"
#include "image_utils.h"
#include "logger.h"
#include "timer_util.h"
#include "ffmpeg_utils.h"
#include <unistd.h>

RpcService::RpcService(const std::string& dev_node,const std::string& img_path)
    : dev_node_(dev_node),img_path_(img_path),state_(ServiceState::UNINITIALIZED)
{
    cap_controller_ = std::make_unique<CaptureController>(dev_node_);
}
RpcService::~RpcService(){
    close();
}

int RpcService::init(){
    try {
        std::string server_rep_addr = "tcp://*:" + std::to_string(RPC_REP_PORT);
        std::string server_pub_addr = "tcp://*:" + std::to_string(RPC_PUB_PORT);
        state_ = ServiceState::UNINITIALIZED;

        // server
        server_ = std::make_unique<hub::RpcServer>(server_rep_addr,4);

        // publisher
        hub::RpcEngine::getInstance().init_publisher(server_pub_addr);
        pub_stream_id_ = hub::RpcEngine::getInstance().create_stream("camera_frames");

        // rgister methods
        register_all_methods();

        server_->start();
        LOG_INFO("RPC Server started on: %s",server_rep_addr.c_str());
        LOG_INFO("RPC Publisher started on: %s",server_pub_addr.c_str());
        return 0;
    } catch (const std::exception& e) {
        LOG_ERROR("RPC Server init failed: %s", e.what());
        return -1;
    }
}

void RpcService::register_all_methods(){

    server_->register_method("init", [this](const CameraConfig& config) -> RpcCMDResponse {
        return this->handle_init(config);
    });

    server_->register_method("run", [this]() -> RpcCMDResponse {
        return this->handle_run();
    });
    server_->register_method("push", [this](const CameraConfig& config) -> RpcCMDResponse {
        return this->handle_push(config);
    });
    server_->register_method("stop", [this]() -> RpcCMDResponse {
        return this->handle_stop();
    });

}


RpcCMDResponse RpcService::handle_init(const CameraConfig& config)
{
    RpcCMDResponse resp;
    std::lock_guard<std::mutex> lock(state_mtx_);

    if (state_ == ServiceState::RUNNING) {
        resp.success = false;
        resp.error_msg = "Invalid state: cannot init while running";
        return resp;
    }

    int ret = cap_controller_->init(config);
    if (ret < 0) {
        resp.success = false;
        resp.error_msg = "Camera init failed";
        return resp;
    }

    cur_config_ = config;
    state_ = ServiceState::INITIALIZED;

    resp.success = true;
    resp.error_msg =
        "Camera init success (width=" + std::to_string(config.width) +
        ", height=" + std::to_string(config.height) + ")";

    return resp;
}

RpcCMDResponse RpcService::handle_run(){
    RpcCMDResponse resp;
    int ret;
    std::vector<uint8_t> dst_frame;
    ImageMeta meta{};
    std::lock_guard<std::mutex> lock(state_mtx_);

    if (state_ != ServiceState::INITIALIZED) {
        resp.success = false;
        resp.error_msg = "Invalid state: run only allowed in INITIALIZED state (current: " + 
            std::to_string(static_cast<int>(state_)) + ")";
        LOG_ERROR(resp.error_msg.c_str());
        return resp;
    }

    switch (cur_config_.cap_mode)
    {
    case CaptureMode::SINGLE:
        ret = cap_controller_->capture_frame_single(dst_frame,meta);
        if(ret < 0){
            resp.success = false;
            resp.error_msg = "Single frame capture failed";
        }
        LOG_INFO("Single frame capture success (size: %lu bytes)", dst_frame.size());
        resp.success = true;
        resp.error_msg = "Single frame capture success";
        resp.image_data.meta = meta;
        resp.image_data.image_data = std::move(dst_frame);
        break;
    case CaptureMode::MULTIPLE:
        if (is_capturing_) {
            resp.success = false;
            resp.error_msg = "Continuous capture is already running";
            break;
        }

        // 设置RPC发布流ID
        cap_controller_->set_pub_stream_id(pub_stream_id_);

        ret = cap_controller_->start_capture_multiple();
        if (ret < 0) {
            resp.success = false;
            resp.error_msg = "Start continuous capture failed";
            LOG_ERROR(resp.error_msg.c_str());
            break;
        }

        is_capturing_ = true;
        state_ = ServiceState::RUNNING;
        resp.success = true;
        resp.error_msg = "Continuous capture started (fps: " + std::to_string(cur_config_.fps) + ")";
        LOG_INFO(resp.error_msg.c_str());
        break;
    default:
        resp.success = false;
        resp.error_msg = "Unsupported capture mode: " + std::to_string(static_cast<int>(cur_config_.cap_mode));
        LOG_ERROR(resp.error_msg.c_str());
        break;
    }

    return resp;
}


RpcCMDResponse RpcService::handle_push(const CameraConfig& config){

    RpcCMDResponse resp;
    std::lock_guard<std::mutex> lock(state_mtx_);

    if (state_ == ServiceState::RUNNING) {
        resp.success = false;
        resp.error_msg = "Invalid state: already in RUNNING state (current: " + 
            std::to_string(static_cast<int>(state_)) + ")";
        LOG_ERROR(resp.error_msg.c_str());
        return resp;
    }

    // 启动RTSP推流
    int ret = cap_controller_->start_rtsp_pusher(config);
    if (ret != 0) {
        resp.success = false;
        resp.error_msg = "启动RTSP推流失败";
        LOG_ERROR("RPC push失败，错误码：%d", ret);
        return resp;
    }

    state_ = ServiceState::RUNNING;
    resp.success = true;
    resp.error_msg = "Rtsp pusher started success";

    return resp;
}


RpcCMDResponse RpcService::handle_stop(){
    RpcCMDResponse resp;
    std::lock_guard<std::mutex> lock(state_mtx_);

    if (state_ == ServiceState::UNINITIALIZED) {
        resp.success = false;
        resp.error_msg = "Invalid state: stop not allowed in UNINITIALIZED state";
        LOG_ERROR(resp.error_msg.c_str());
        return resp;
    }

    if (is_capturing_) {
        cap_controller_->stop_capture_multiple();
        
        is_capturing_ = false;
        if (capture_thread_.joinable()) {
            capture_thread_.join();
        }
    }
    cap_controller_->stop_rtsp_pusher();
    cap_controller_->release();
    
    state_ = ServiceState::UNINITIALIZED;
    resp.success = true;
    resp.error_msg = "Capture stopped and camera resource released";
    LOG_INFO(resp.error_msg.c_str());
    return resp;
}

int RpcService::close(){

    std::lock_guard<std::mutex> lock(state_mtx_);
    if (is_capturing_) {
        cap_controller_->stop_capture_multiple();
        is_capturing_ = false;
        if (capture_thread_.joinable()) {
            capture_thread_.join();
        }
    }
    if (cap_controller_) {
        cap_controller_->release();
    }
    state_ = ServiceState::UNINITIALIZED; 

    // 停止RPC
    if(server_){
        server_->stop();
        LOG_INFO("RPC Server stopped");
    }
    return 0;
}