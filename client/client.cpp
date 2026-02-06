#include "utils/timer_util.h"
#include "utils/tcp_helper.h"
#include "utils/zmq_helper.h"
#include "utils/data_type.h"
#include <iostream>
#include <opencv2/opencv.hpp>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <netinet/tcp.h>

bool decompressImage(const std::vector<uchar>& comp_data, const ImageMeta& meta, cv::Mat& dst_img) {
    if(!meta.is_compressed) return false;
    dst_img = cv::imdecode(comp_data, cv::IMREAD_COLOR);
    return !dst_img.empty();
}

void runTcpClient(const std::string& server_ip, int port) {

    std::unique_ptr<TcpHelper> tcpServer = std::make_unique<TcpHelper>();
    int client_fd = tcpServer->create_client_socket();
    if (client_fd < 0) return;

    if (!tcpServer->connect_server(client_fd, server_ip, port)) {
        tcpServer->close_socket(client_fd);
        return;
    }

    cv::Mat receivedImg;
    for(int i=0;i < 10;i++){
        Request req{};
        req.seq = i;

        TimerUtil total_timer;
        total_timer.start();

        if (!tcpServer->send_msg(client_fd, &req, sizeof(Request))) {
            tcpServer->close_socket(client_fd);
            return;
        }
        // std::cout << "Sent request, seq = " << i << std::endl;

        ImageMeta meta{};       
        if (!tcpServer->recv_msg(client_fd, &meta, sizeof(ImageMeta))) {
            tcpServer->close_socket(client_fd);
            return;
        }

        if(meta.is_compressed)
        {
            std::vector<uchar> compressed_data(meta.comp_data_len);
            size_t received = 0;
            while (received < meta.comp_data_len) {
                ssize_t n = recv(client_fd, compressed_data.data() + received, meta.comp_data_len - received, 0);
                if (n <= 0) break;
                received += n;
            }
            // 解压图片
            TimerUtil decodeTimer;
            decodeTimer.start();
            decompressImage(compressed_data, meta, receivedImg);
            decodeTimer.end("Decode Image");


        } else {
            receivedImg = cv::Mat(meta.rows, meta.cols, meta.type);
            size_t total = receivedImg.total() * receivedImg.elemSize();
            size_t received = 0;
            while (received < total) {
                ssize_t n = recv(client_fd, receivedImg.data + received, total - received, 0);
                if (n <= 0) break;
                received += n;
            }
        }

        total_timer.end("Image received");

    }

    // 保存图片
    bool ok = cv::imwrite("../image/received.jpg",receivedImg);
    if(!ok){
        std::cerr << "save image failed" << std::endl;
        return;
    }
    tcpServer->close_socket(client_fd);
}

void runZMQClient(const std::string& server_ip, int port) {

    std::unique_ptr<ZMQHelper> zmqServer = std::make_unique<ZMQHelper>();
    std::string server_addr = "tcp://" + server_ip + ":" + std::to_string(port);
    zmq::socket_t req_socket = zmqServer->create_req_socket(server_addr);

    std::cout << "Connected to server successfully!" << std::endl;

    for (uint32_t i = 0; i < 10; ++i) {
        TimerUtil totalTimer;
        totalTimer.start();

        Request req{i};
        zmq::message_t req_msg(&req, sizeof(Request));
        zmqServer->sendMsg(req_socket, req_msg);

        zmq::message_t meta_msg;
        zmqServer->recvMsg(req_socket, meta_msg);
        ImageMeta* meta = static_cast<ImageMeta*>(meta_msg.data());

        cv::Mat img;
        if(meta->is_compressed)
        {
            zmq::message_t img_msg;
            zmqServer->recvMsg(req_socket, img_msg);

            std::vector<uchar> compressed_data(img_msg.size());
            memcpy(compressed_data.data(), img_msg.data(), img_msg.size());
            // 解压图片
            TimerUtil decodeTimer;
            decodeTimer.start();
            decompressImage(compressed_data, *meta, img);
            decodeTimer.end("Decode Image");
        } else {

            zmq::message_t img_msg;
            zmqServer->recvMsg(req_socket, img_msg);
            img = cv::Mat(meta->rows, meta->cols, meta->type);
            memcpy(img.data, img_msg.data(), img.total() * img.elemSize());
        }

        totalTimer.end("image received");
    }
}

int main() {

    runTcpClient(SERVER_IP_RK3576.c_str(),TCP_PORT);
    //runZMQClient(SERVER_IP.c_str(),ZMQ_PORT);
    return 0;
}