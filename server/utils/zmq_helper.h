#pragma once

#include <zmq.hpp>
#include <string>
#include <iostream>

const int ZMQ_RECV_TIMEOUT = 5000;
const int ZMQ_SEND_TIMEOUT = 5000;

class ZMQHelper {
public:
    ZMQHelper();
    ~ZMQHelper();

    zmq::socket_t create_req_socket(const std::string& server_addr);
    zmq::socket_t create_rep_socket(const std::string& bind_addr);
    zmq::socket_t create_sub_socket(const std::string& server_addr);
    zmq::socket_t create_pub_socket(const std::string& bind_addr);

    bool sendMsg(zmq::socket_t& socket, zmq::message_t& msg);
    bool sendMsg(zmq::socket_t& socket, zmq::message_t& msg, zmq::send_flags flag);
    bool recvMsg(zmq::socket_t& socket, zmq::message_t& msg);

private:
    zmq::context_t m_context;
};
