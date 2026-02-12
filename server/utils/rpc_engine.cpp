#include "rpc_engine.h"
#include <iostream>
#include <stdexcept>

namespace hub{

RpcEngine::RpcEngine(){
    zmq_ctx_ = std::make_unique<zmq::context_t>(2);
}

RpcEngine::~RpcEngine(){
    stop_server();
    close_client();
}

RpcEngine::SharedPtr RpcEngine::createInstance(){
    return SharedPtr(new RpcEngine());
}

RpcEngine& RpcEngine::getInstance(){
    static RpcEngine instance;
    return instance;
}

uint64_t RpcEngine::generate_request_id() noexcept{
    return request_id_seq_.fetch_add(1, std::memory_order_acq_rel);
}

// 服务端初始化
bool RpcEngine::init_server(const std::string& listen_addr,int io_threads){
    try {
        zmq_ctx_->set(zmq::ctxopt::io_threads, io_threads);
        router_socket_ = std::make_unique<zmq::socket_t>(*zmq_ctx_, ZMQ_ROUTER);
        dealer_socket_ = std::make_unique<zmq::socket_t>(*zmq_ctx_, ZMQ_DEALER);

        const int hwm = 10000;
        router_socket_->set(zmq::sockopt::sndhwm, hwm);
        router_socket_->set(zmq::sockopt::rcvhwm, hwm);
        router_socket_->set(zmq::sockopt::tcp_keepalive, 1);
        router_socket_->set(zmq::sockopt::linger, 100);
        router_socket_->set(zmq::sockopt::router_mandatory, 1);
        router_socket_->set(zmq::sockopt::heartbeat_ivl, 5000);
        router_socket_->set(zmq::sockopt::heartbeat_timeout, 15000);
        router_socket_->set(zmq::sockopt::immediate, 1);

        dealer_socket_->set(zmq::sockopt::sndhwm, hwm);
        dealer_socket_->set(zmq::sockopt::rcvhwm, hwm);
        dealer_socket_->set(zmq::sockopt::linger, 100);

        router_socket_->bind(listen_addr);
        dealer_socket_->bind("inproc://rpc_workers");
        return true;
    } catch (const std::exception& e) {
        std::cerr << "ZMQ server init error: " << e.what() << std::endl;
        return false;
    }
}

void RpcEngine::register_method(const std::string& method_name,RpcHandler handler){
    std::unique_lock<std::shared_mutex> lock(method_mutex_);
    method_map_[method_name] = std::move(handler);
}

void RpcEngine::proxy_routine() {
    zmq_proxy(router_socket_->handle(), dealer_socket_->handle(), nullptr);
}

void RpcEngine::worker_routine() {
    zmq::socket_t worker(*zmq_ctx_, ZMQ_DEALER);
    worker.connect("inproc://rpc_workers");

    while (server_running_) {
        zmq::message_t identity, req_msg;
        if (!worker.recv(identity) || !worker.recv(req_msg)) continue;

        try {
            const auto req = deserialize<RpcRequest>(req_msg);
            auto resp = handle_request(req);
            resp.request_id = req.request_id;
            worker.send(identity, zmq::send_flags::sndmore);
            worker.send(serialize(resp), zmq::send_flags::none);
        } catch (const zmq::error_t& e) {
            if (!server_running_) break;
            continue;
        } catch (...) {
            continue;
        }
    }
    try { worker.close(); } catch (...) {}
}

RpcResponse RpcEngine::handle_request(const RpcRequest& request) {
    
    std::cout << "[RPC] request_id=" << request.request_id
              << ", method=" << request.method
              << ", params_size=" << request.params_data.size()
              << std::endl;

    std::shared_lock<std::shared_mutex> lock(method_mutex_);
    auto it = method_map_.find(request.method);
    if (it == method_map_.end()) {
        return {request.request_id, RpcErrorCode::MethodNotFound, {}, "Method not found: " + request.method};
    }
    lock.unlock();
    return it->second(request.params_data);
}

void RpcEngine::start_server(size_t worker_threads){
    if(!router_socket_ || !dealer_socket_)
        throw std::runtime_error("Server not initialized");
    server_running_ = true;
    proxy_thread_ = std::thread(&RpcEngine::proxy_routine, this);
    for(size_t i = 0; i < worker_threads; ++i){
        worker_threads_.emplace_back(&RpcEngine::worker_routine, this);
    }
}

void RpcEngine::stop_server() noexcept{
    server_running_ = false;
    // 中断zmq_proxy
    if(router_socket_) router_socket_->close();
    if(dealer_socket_) dealer_socket_->close();
    if(pub_socket_) pub_socket_->close();


    if(proxy_thread_.joinable()) proxy_thread_.join();
    for(auto& t : worker_threads_) if(t.joinable()) t.join();
    worker_threads_.clear();
}

// PUB
bool RpcEngine::init_publisher(const std::string& bind_addr){
    try
    {
        if(!zmq_ctx_){
            zmq_ctx_ = std::make_unique<zmq::context_t>(2);
        }

        pub_socket_ = std::make_unique<zmq::socket_t>(*zmq_ctx_,ZMQ_PUB);

        const int hwm = 10000;
        pub_socket_->set(zmq::sockopt::sndhwm, hwm);
        pub_socket_->set(zmq::sockopt::linger,100);
        pub_socket_->bind(bind_addr);
        pub_bind_addr_ = bind_addr;

        return true;
    } catch (const zmq::error_t& e) {
        std::cerr << "ZMQ publisher init error: " << e.what() << std::endl;
        return false;
    } catch (const std::exception& e) {
        std::cerr << "Publisher init exception: " << e.what() << std::endl;
        return false;
    }
    
}
uint64_t RpcEngine::create_stream(const std::string& topic){
    uint64_t id = stream_id_seq_.fetch_add(1,std::memory_order_relaxed);
    StreamContext ctx;
    ctx.topic = topic;
    ctx.seq = 0;
    {
        std::lock_guard<std::mutex> lk(stream_mutex_);
        streams_.emplace(id, std::move(ctx));
    }

    return id;
}
bool RpcEngine::publish(uint64_t stream_id, const void* data, size_t size){
    if(!pub_socket_) return false;

    StreamContext ctx;
    {
        std::lock_guard<std::mutex> lock(stream_mutex_);
        auto it = streams_.find(stream_id);
        if(it == streams_.end()){
            std::cerr << "Stream not found,id : " << stream_id << std::endl;
            return false;
        }

        ctx.topic = it->second.topic;
        ctx.seq = it->second.seq;

        it->second.seq++;
    }

    // 填充stream meta
    StreamMeta meta;
    meta.stream_id = stream_id;
    meta.seq = ctx.seq;
    // meta.timestamp = (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
    //     std::chrono::system_clock::now().time_since_epoch()).count();

    try
    {
        // topic-meta-payload
        pub_socket_->send(zmq::buffer(ctx.topic),zmq::send_flags::sndmore);
        pub_socket_->send(serialize(meta),zmq::send_flags::sndmore);
        pub_socket_->send(zmq::buffer(data,size),zmq::send_flags::none);

        return true;
    } catch (const zmq::error_t& e) {
        std::cerr << "publish zmq error: " << e.what() << std::endl;
        return false;
    } catch (...) {
        return false;
    }
    

}
void RpcEngine::close_stream(uint64_t stream_id) noexcept{
    std::lock_guard<std::mutex> lk(stream_mutex_);
    auto it = streams_.find(stream_id);
    if (it != streams_.end()) streams_.erase(it);
}




// 客户端初始化
bool RpcEngine::init_client(const std::string& server_addr, int timeout_ms) {
    try {
        client_dealer_ = std::make_unique<zmq::socket_t>(*zmq_ctx_, ZMQ_DEALER);
        client_timeout_ = timeout_ms;

        client_dealer_->set(zmq::sockopt::tcp_keepalive, 1);
        client_dealer_->set(zmq::sockopt::linger, 100);
        client_dealer_->connect(server_addr);

        client_running_ = true;
        client_recv_thread_ = std::thread(&RpcEngine::client_recv_routine, this);
        return true;
    } catch (const zmq::error_t& e) {
        std::cerr << "ZMQ client init error: " << e.what() << std::endl;
        return false;
    }
}

// 客户端接收线程
void RpcEngine::client_recv_routine() {
    while (client_running_) {
        try {
            zmq::message_t resp_msg;
            if (!client_dealer_->recv(resp_msg)) continue;

            const auto resp = deserialize<RpcResponse>(resp_msg);
            std::lock_guard<std::mutex> lock(pending_mutex_);
            auto it = pending_requests_.find(resp.request_id);
            if (it != pending_requests_.end()) {
                it->second.set_value(resp);
                pending_requests_.erase(it);
            }
        } catch (const zmq::error_t& e) {
            if (!client_running_) break;
            continue;
        } catch (...) {
            continue;
        }
    }
}

void RpcEngine::close_client() noexcept {
    client_running_ = false;
    // zmq_ctx_->shutdown();
    if (client_dealer_) client_dealer_->close();
    if (client_recv_thread_.joinable()) client_recv_thread_.join();
    

    std::lock_guard<std::mutex> lock(pending_mutex_);
    pending_requests_.clear();
}

// RpcClient 实现
RpcClient::RpcClient(const std::string& server_addr, int timeout_ms) {
    rpc_ = RpcEngine::createInstance();
    if (!rpc_->init_client(server_addr, timeout_ms)) {
        throw std::runtime_error("Init client failed");
    }
}
RpcClient::~RpcClient() = default;

// RpcServer 实现
RpcServer::RpcServer(const std::string& listen_addr, size_t worker_threads)
    : worker_threads_(worker_threads) {
    rpc_ = RpcEngine::createInstance();
    if (!rpc_->init_server(listen_addr)) {
        throw std::runtime_error("Init server failed");
    }
}
RpcServer::~RpcServer() = default;
void RpcServer::start() { rpc_->start_server(worker_threads_); }
void RpcServer::stop() noexcept { rpc_->stop_server(); }


StreamSubscriber::StreamSubscriber(zmq::context_t& ctx) : ctx_(ctx), running_(false) {}

StreamSubscriber::~StreamSubscriber() { unsubscribe(); }

bool StreamSubscriber::subscribe(const std::string& addr, const std::string& topic, Callback cb) {
    if (running_) return false;
    callback_ = std::move(cb);
    sub_socket_ = std::make_unique<zmq::socket_t>(ctx_, ZMQ_SUB);
    sub_socket_->set(zmq::sockopt::subscribe, topic);
    sub_socket_->connect(addr);
    running_ = true;
    recv_thread_ = std::thread(&StreamSubscriber::recv_loop, this);
    return true;
}

void StreamSubscriber::unsubscribe() {
    running_ = false;

    if (recv_thread_.joinable()) recv_thread_.join();
    
    if (sub_socket_) {
        try { sub_socket_->close(); } catch(...) {}
        sub_socket_.reset();
    }
    
}


void StreamSubscriber::recv_loop() {
    while (running_) {
        try {
            zmq::message_t topic_msg;
            zmq::message_t meta_msg;
            zmq::message_t payload_msg;

            if(!sub_socket_->recv(topic_msg)) continue; // topic
            if(!sub_socket_->recv(meta_msg)) continue;  // meta
            if(!sub_socket_->recv(payload_msg)) continue; // payload

            StreamMeta meta = hub::RpcEngine::deserialize<StreamMeta>(meta_msg);
            std::vector<uint8_t> payload((uint8_t*)payload_msg.data(),
                                             (uint8_t*)payload_msg.data() + payload_msg.size());

            if (callback_) callback_(meta, payload);
        } catch (const zmq::error_t& e) {
            if (!running_) break;
            continue;
        } catch (...) {
            continue;
        }
    }
}



} // namespace hub