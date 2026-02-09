#pragma

#include "rpc_engine.h"
#include "data_type.h"
#include "rpc_engine.h"
#include "capture_controller.h"

enum class ServiceState {
    UNINITIALIZED,  
    INITIALIZED,    
    RUNNING
};

class RpcService{
public:
    explicit RpcService(const std::string& dev_node,const std::string& img_path);
    ~RpcService();

    int init();
    
    int close();

private:

    RpcCMDResponse handle_init(const CameraConfig& config);
    RpcCMDResponse handle_run();
    RpcCMDResponse handle_stop();
    void register_all_methods();

    std::unique_ptr<CaptureController> cap_controller_;
    std::unique_ptr<hub::RpcServer> server_;
    std::string dev_node_;
    std::string img_path_;

    CameraConfig cur_config_;
    ServiceState state_;
    std::mutex state_mtx_; 

    std::atomic<bool> is_capturing_{false};
    std::thread capture_thread_;
    uint64_t pub_stream_id_{0};
};

MSGPACK_ADD_ENUM(ServiceState);