#include "zmq_helper.h"

ZMQHelper::ZMQHelper() : m_context(1) {
    std::cout << "[ZMQHelper] Context initialized (1 IO thread)" << std::endl;
}

ZMQHelper::~ZMQHelper() {
    std::cout << "[ZMQHelper] Context destroyed" << std::endl;
}

zmq::socket_t ZMQHelper::create_req_socket(const std::string& server_addr) {
    zmq::socket_t socket(m_context, zmq::socket_type::req);
    socket.set(zmq::sockopt::rcvtimeo, ZMQ_RECV_TIMEOUT);
    socket.connect(server_addr);
    std::cout << "[ZMQHelper] REQ socket connected to " << server_addr << std::endl;
    return socket;
}

zmq::socket_t ZMQHelper::create_rep_socket(const std::string& bind_addr) {
    zmq::socket_t socket(m_context, zmq::socket_type::rep);
    socket.set(zmq::sockopt::rcvtimeo, ZMQ_RECV_TIMEOUT);
    socket.bind(bind_addr);
    std::cout << "[ZMQHelper] REP socket bound to " << bind_addr << std::endl;
    return socket;
}

zmq::socket_t ZMQHelper::create_sub_socket(const std::string& server_addr) {
    zmq::socket_t socket(m_context, zmq::socket_type::sub);
    socket.set(zmq::sockopt::rcvtimeo, ZMQ_RECV_TIMEOUT);
    socket.set(zmq::sockopt::subscribe, ""); 
    socket.connect(server_addr);
    std::cout << "[ZMQHelper] SUB socket connected to " << server_addr << " (all sub)" << std::endl;
    return socket;
}

zmq::socket_t ZMQHelper::create_pub_socket(const std::string& bind_addr) {
    zmq::socket_t socket(m_context, zmq::socket_type::pub);
    socket.set(zmq::sockopt::rcvtimeo, ZMQ_RECV_TIMEOUT);
    socket.set(zmq::sockopt::sndtimeo, ZMQ_SEND_TIMEOUT);
    socket.set(zmq::sockopt::conflate, 1);
    socket.set(zmq::sockopt::sndhwm, 10);
    socket.bind(bind_addr);
    std::cout << "[ZMQHelper] PUB socket bound to " << bind_addr << std::endl;
    return socket;
}

bool ZMQHelper::sendMsg(zmq::socket_t& socket, zmq::message_t& msg) {
    auto res = socket.send(msg, zmq::send_flags::none);
    if (!res) {
        std::cerr << "[ZMQHelper] sendMessage failed, message not sent completely" << std::endl;
        return false;
    }
    return true;
}

bool ZMQHelper::sendMsg(zmq::socket_t& socket, zmq::message_t& msg, zmq::send_flags flag) {
    auto res = socket.send(msg, flag);
    if (!res) {
        std::cerr << "[ZMQHelper] sendMessage failed with flag, message not sent completely" << std::endl;
        return false;
    }
    return true;
}

bool ZMQHelper::recvMsg(zmq::socket_t& socket, zmq::message_t& msg) {
    auto res = socket.recv(msg);
    if (!res) {
        std::cerr << "[ZMQHelper] recvMessage failed or timeout" << std::endl;
        return false;
    }
    return true;
}
