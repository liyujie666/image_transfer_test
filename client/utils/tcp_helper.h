#pragma once
#include "data_type.h"
#include <string>
#include <cstdint>

class TcpHelper {
public:
    TcpHelper();
    ~TcpHelper();

    // server
    int create_and_listen_server(int port);
    int accept_client(int server_fd);
    bool recv_msg(int client_fd, void* buf, size_t buf_len);
    bool send_msg(int client_fd, const void* buf, size_t buf_len);
    void close_socket(int fd);

    // client
    int create_client_socket();
    bool connect_server(int client_fd, const std::string& server_ip, int port);

private:
};