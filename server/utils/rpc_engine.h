#pragma once
#define MSGPACK_USE_CPP17
#define MSGPACK_API_VERSION 3
#define MSGPACK_DISABLE_LEGACY_NI
#include <string>
#include <functional>
#include <unordered_map>
#include <shared_mutex>
#include <memory>
#include <vector>
#include <future>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <exception>
#include <tuple>
#include <type_traits>
#include <zmq.hpp>
#include <msgpack.hpp>

static_assert(__cplusplus >= 201703L, "Requires C++17 or later");

namespace hub
{
enum class RpcErrorCode{
    Success = 0,
    MethodNotFound,
    SerializeError,
    Timeout,
    NetworkError,
    InvalidRequest,
    BusinessException
};

struct RpcResponse{
    uint64_t request_id;
    RpcErrorCode code;
    std::string result_data; 
    std::string error_msg;
    MSGPACK_DEFINE(request_id, code, result_data, error_msg);
};

struct RpcRequest{
    uint64_t request_id;
    std::string method;
    std::string params_data;  
    MSGPACK_DEFINE(request_id, method, params_data);
};

struct StreamMeta {
    uint64_t stream_id;
    uint64_t seq;
    uint64_t timestamp;
    std::string payload; 
    MSGPACK_DEFINE(stream_id, seq, timestamp, payload);
};

struct StreamContext {
    std::string topic;
    uint64_t seq{0};
};

// 函数类型萃取工具
template<typename T>
struct function_traits : public function_traits<decltype(&T::operator())> {};

template<typename Ret,typename... Args>
struct function_traits<Ret(Args...)> {
    using return_type = Ret;
    using args_tuple =  std::tuple<std::decay_t<Args>...>;
    static constexpr size_t args_count = sizeof...(Args);
};

template<typename Ret,typename... Args>
struct function_traits<Ret(*)(Args...)> : public function_traits<Ret(Args...)> {};

template<typename Cls,typename Ret,typename...Args>
struct function_traits<Ret(Cls::*)(Args...)> : public function_traits<Ret(Args...)> {};

template<typename Cls, typename Ret, typename... Args>
struct function_traits<Ret(Cls::*)(Args...) const> : function_traits<Ret(Args...)> {};

class RpcEngine{
public:
    using SharedPtr = std::shared_ptr<RpcEngine>;
    using RpcHandler = std::function<RpcResponse(const std::string&)>;

    static SharedPtr createInstance();
    static RpcEngine& getInstance();

    ~RpcEngine();
    RpcEngine(const RpcEngine&) = delete;
    RpcEngine& operator=(const RpcEngine&) = delete;
    RpcEngine(RpcEngine&&) = delete;
    RpcEngine& operator=(RpcEngine&&) = delete;

    /**
     * 服务端接口
    */
    // Router/Dealer 
    bool init_server(const std::string& listen_addr,int io_threads = 2);
    void register_method(const std::string& method_name,RpcHandler handler);
    void start_server(size_t worker_threads = 4);
    void stop_server() noexcept;
    // PUB
    bool init_publisher(const std::string& bind_addr);
    uint64_t create_stream(const std::string& topic);
    bool publish(uint64_t stream_id, const void* data, size_t size);
    void close_stream(uint64_t stream_id) noexcept;


    /**
     * 客户端接口
    */
    // Router/Dealer
    bool init_client(const std::string& server_addr,int timeout_ms = 3000);
    template<typename... Args>
    RpcResponse call(const std::string& method_name,int timeout_ms,Args&&... args);
    template<typename... Args>
    std::future<RpcResponse> call_async(const std::string& method_name, Args&&... args);
    void close_client() noexcept;
    // SUB


    // 序列化/反序列化工具
    template<typename T>
    static zmq::message_t serialize(const T& data);
    template<typename T>
    static T deserialize(const zmq::message_t& msg);

private:
    RpcEngine();
    uint64_t generate_request_id() noexcept;

    void proxy_routine();
    void worker_routine();
    void client_recv_routine();

    // 请求分发
    RpcResponse handle_request(const RpcRequest& request);

    // ZMQ 上下文与套接字
    std::unique_ptr<zmq::context_t> zmq_ctx_;
    std::unique_ptr<zmq::socket_t> router_socket_;
    std::unique_ptr<zmq::socket_t> dealer_socket_;
    std::unique_ptr<zmq::socket_t> client_dealer_;
    std::unique_ptr<zmq::socket_t> pub_socket_;

    // 方法注册表
    std::unordered_map<std::string,RpcHandler> method_map_;
    mutable std::shared_mutex method_mutex_;

    // 线程管理
    std::vector<std::thread> worker_threads_;
    std::thread proxy_thread_;
    std::thread client_recv_thread_;

    // 异步请求管理
    std::unordered_map<uint64_t,std::promise<RpcResponse>> pending_requests_;
    std::mutex pending_mutex_;

    // PUB stream 管理
    std::mutex stream_mutex_;
    std::unordered_map<uint64_t, StreamContext> streams_;
    
    std::atomic<bool> server_running_{false};
    std::atomic<bool> client_running_{false};
    std::atomic<uint64_t> request_id_seq_{1};
    std::atomic<uint64_t> stream_id_seq_{1};
    int client_timeout_{3000};
    std::string pub_bind_addr_;
};

class RpcClient{
public:
    explicit RpcClient(const std::string& server_addr,int timeout_ms = 3000);
    ~RpcClient();

    template<typename... Args>
    RpcResponse call(const std::string& method_name,int timeout_ms,Args&&... args);
    template<typename... Args>
    RpcResponse call(const std::string& method_name, Args&&... args);
    template<typename... Args>
    std::future<RpcResponse> call_async(const std::string& method_name, Args&&... args);

private:
    RpcEngine::SharedPtr rpc_;
};

class RpcServer{
public:
    RpcServer(const std::string& listen_addr, size_t worker_threads = 4);
    ~RpcServer();

    template<typename Func>
    void register_method(const std::string& method_name,Func&& func);
    void start();
    void stop() noexcept;

private:
    RpcEngine::SharedPtr rpc_;
    size_t worker_threads_;
};


class StreamSubscriber {
public:
    using Callback = std::function<void(const StreamMeta&, const std::vector<uint8_t>&)>;

    StreamSubscriber(zmq::context_t& ctx);
    ~StreamSubscriber();

    bool subscribe(const std::string& addr, const std::string& topic, Callback cb);
    void unsubscribe();

private:
    void recv_loop();
    zmq::context_t& ctx_;
    std::unique_ptr<zmq::socket_t> sub_socket_;
    std::thread recv_thread_;
    std::atomic<bool> running_;
    Callback callback_;
};

// 序列化实现
template<typename T>
zmq::message_t RpcEngine::serialize(const T& data){
    msgpack::sbuffer buffer;
    msgpack::pack(buffer, data);
    return zmq::message_t(buffer.data(), buffer.size());
}

// 反序列化实现
template<typename T>
T RpcEngine::deserialize(const zmq::message_t& msg){
    try {
        auto handle = msgpack::unpack(static_cast<const char*>(msg.data()), msg.size());
        return handle->as<T>();
    } catch (...) {
        throw std::runtime_error("msgpack deserialize failed");
    }
}

// 客户端同步调用
template<typename... Args>
RpcResponse RpcEngine::call(const std::string& method_name, int timeout_ms, Args&&... args){
    auto future = call_async(method_name, std::forward<Args>(args)...);
    auto status = future.wait_for(std::chrono::milliseconds(timeout_ms));
    if(status == std::future_status::timeout){
        return {0, RpcErrorCode::Timeout, {}, "Call timeout"};
    }
    return future.get();
}

// 客户端异步调用
template<typename... Args>
std::future<RpcResponse> RpcEngine::call_async(const std::string& method_name, Args&&... args){
    if(!client_running_){
        std::promise<RpcResponse> p;
        p.set_value({0, RpcErrorCode::NetworkError, {}, "Client not initialized"});
        return p.get_future();
    }

    RpcRequest req;
    req.request_id = generate_request_id();
    req.method = method_name;

    const auto args_tuple = std::make_tuple(std::forward<Args>(args)...);
    msgpack::sbuffer buf;
    msgpack::pack(buf, args_tuple);
    req.params_data.assign(buf.data(), buf.size());

    // 注册异步请求
    std::promise<RpcResponse> promise;
    auto future = promise.get_future();
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_requests_.emplace(req.request_id, std::move(promise));
    }

 
    client_dealer_->send(serialize(req), zmq::send_flags::none);
    return future;
}

// 服务端方法注册
template<typename Func>
void RpcServer::register_method(const std::string& method_name, Func&& func){
    using Traits = function_traits<std::decay_t<Func>>;
    using ArgsTuple = typename Traits::args_tuple;
    using RetType = typename Traits::return_type;

    rpc_->register_method(method_name, [func = std::forward<Func>(func)](const std::string& params_data) -> RpcResponse {
        try {
            // 反序列化参数
            ArgsTuple args;
            msgpack::unpack(params_data.data(), params_data.size()).get().convert(args);

            msgpack::sbuffer result_buf;
            if constexpr(std::is_same_v<RetType, void>){
                std::apply(func, std::move(args));
            } else {
                auto result = std::apply(func, std::move(args));
                msgpack::pack(result_buf, result);
            }
            return {0, RpcErrorCode::Success, {result_buf.data(), result_buf.size()}, ""};
            
        } catch (const std::exception& e) {
            return {0, RpcErrorCode::BusinessException, {}, "Handler error: " + std::string(e.what())};
        }
    });
}

// RpcClient 包装接口
template<typename... Args>
RpcResponse RpcClient::call(const std::string& method_name, Args&&... args){
    return call(method_name, 3000, std::forward<Args>(args)...);
}

template<typename... Args>
RpcResponse RpcClient::call(const std::string& method_name, int timeout_ms, Args&&... args){
    return rpc_->call(method_name, timeout_ms, std::forward<Args>(args)...);
}

template<typename... Args>
std::future<RpcResponse> RpcClient::call_async(const std::string& method_name, Args&&... args) {
    return rpc_->call_async(method_name, std::forward<Args>(args)...);
}

} // namespace hub

MSGPACK_ADD_ENUM(hub::RpcErrorCode);