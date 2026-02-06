#include "tcp_helper.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <cstring>
#include <netinet/tcp.h>
#include <iostream>
#include <cerrno>

TcpHelper::TcpHelper() {
    std::cout << "[TcpHelper] TCP Helper initialized" << std::endl;
}

TcpHelper::~TcpHelper() {
    std::cout << "[TcpHelper] TCP Helper destroyed" << std::endl;
}

int TcpHelper::create_and_listen_server(int port) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("[TcpHelper] socket create failed");
        return -1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(server_fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(server_fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("[TcpHelper] bind failed");
        close_socket(server_fd);
        return -1;
    }

    if (listen(server_fd, 5) < 0) {
        perror("[TcpHelper] listen failed");
        close_socket(server_fd);
        return -1;
    }

    std::cout << "[TcpHelper] TCP Server created, listen on port: " << port << std::endl;
    return server_fd;
}

int TcpHelper::accept_client(int server_fd) {
    int client_fd = accept(server_fd, nullptr, nullptr);
    if (client_fd < 0) {
        perror("[TcpHelper] accept client failed");
        return -1;
    }

    int opt = 1;
    setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
    std::cout << "[TcpHelper] Client connected successfully" << std::endl;
    return client_fd;
}

bool TcpHelper::recv_msg(int client_fd, void* buf, size_t buf_len) {
    ssize_t ret = recv(client_fd, buf, buf_len, MSG_WAITALL);
    if (ret <= 0) {
        if(ret < 0) perror("[TcpHelper] recv data error");
        else std::cerr << "[TcpHelper] Client disconnected when recv data" << std::endl;
        return false;
    }
    return true;
}

bool TcpHelper::send_msg(int client_fd, const void* buf, size_t buf_len) {
    size_t sent = 0;
    const char* data_ptr = static_cast<const char*>(buf);
    while (sent < buf_len) {
        ssize_t n = send(client_fd, data_ptr + sent, buf_len - sent, 0);
        if (n <= 0) {
            perror("[TcpHelper] send data error");
            return false;
        }
        sent += n;
    }
    return true;
}

void TcpHelper::close_socket(int fd) {
    if (fd >= 0) {
        close(fd);
        std::cout << "[TcpHelper] Socket fd closed" << std::endl;
    }
}


int TcpHelper::create_client_socket() {
    int client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd < 0) {
        perror("[TcpHelper] create client socket failed");
        return -1;
    }

    int opt = 1;
    setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
    return client_fd;
}

bool TcpHelper::connect_server(int client_fd, const std::string& server_ip, int port) {
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, server_ip.c_str(), &server_addr.sin_addr) <= 0) {
        perror("[TcpHelper] invalid server ip address");
        return false;
    }

    if (connect(client_fd, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("[TcpHelper] connect to server failed");
        return false;
    }

    std::cout << "[TcpHelper] Client connected to server " << server_ip << ":" << port << std::endl;
    return true;
}