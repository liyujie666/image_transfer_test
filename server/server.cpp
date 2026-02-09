#include "utils/debug_global.h"
#include "utils/timer_util.h"
#include "utils/data_type.h"
#include "utils/tcp_helper.h"
#include "utils/zmq_helper.h"
#include "utils/image_utils.h"
#include "utils/ffmpeg_utils.h"
#include "utils/rpc_service.h"
#include "utils/logger.h"
#include <iostream>
#include <opencv2/opencv.hpp>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <cstring>
#include <csignal>


void runTCPServer(const std::vector<uchar>& encoded_data,const ImageMeta& meta) {

    std::unique_ptr<TcpHelper> tcpServer = std::make_unique<TcpHelper>();
    int server_fd = tcpServer->create_and_listen_server(TCP_PORT);
    if (server_fd < 0) return;

    while (true) {
        std::cout << "Waiting for client connection..." << std::endl;
        int client_fd = tcpServer->accept_client(server_fd);
        if (client_fd < 0) continue;

        while (true) {
            Request req{};
            if (!tcpServer->recv_msg(client_fd, &req, sizeof(Request))) {
                std::cout << "Client disconnected or recv failed" << std::endl;
                break;
            }

            TimerUtil totalTimer;
            totalTimer.start();

            // 发送图片元信息 + 发送图片数据
            tcpServer->send_msg(client_fd, &meta, sizeof(ImageMeta));
            tcpServer->send_msg(client_fd, encoded_data.data(), encoded_data.size());
            
            
            totalTimer.end("image sending");
        }

        tcpServer->close_socket(client_fd);
    }
    tcpServer->close_socket(server_fd);
}

void runZMQServer(const std::vector<uchar>& encoded_data,const ImageMeta& meta) {

    std::unique_ptr<ZMQHelper> zmqServer = std::make_unique<ZMQHelper>();
    std::string zmqBindAddr = "tcp://" + SERVER_IP_RK3588 + ":" + std::to_string(ZMQ_PORT);
    zmq::socket_t repSocket = zmqServer->create_rep_socket(zmqBindAddr);

    while (true) {
        std::cout << "Waiting for client request..." << std::endl;
        zmq::message_t reqMsg(sizeof(Request));
        if (!zmqServer->recvMsg(repSocket, reqMsg)) continue;

        TimerUtil totalTimer;
        totalTimer.start();
        

        // 元信息
        zmq::message_t metaMsg(&meta, sizeof(ImageMeta));
        zmqServer->sendMsg(repSocket, metaMsg, zmq::send_flags::sndmore);
        // 压缩后的数据
        zmq::message_t imgMsg(encoded_data.data(), encoded_data.size());
        if (!zmqServer->sendMsg(repSocket, imgMsg)) { 
            std::cerr << "send compressed image failed" << std::endl;
        }

        totalTimer.end("image sending");

    }
}


bool processByImage(const std::string& imgPath,std::vector<uchar>& encoded_data,ImageMeta& meta){
    // read image by opencv
    cv::Mat raw_img;
    if (!loadImageByOpencv(imgPath, raw_img)) {
        return false;
    }

    // conver image(bgr --> yuv420p)
    cv::Mat yuv420pImg;
#if CONVERT_BY_OPENCV

    if(!convertBgrToYuv420pByCv(raw_img,yuv420pImg)){
        return false;
    }

#elif CONVERT_BY_RGA

    if(!convertBgrToYuv420pByRga(raw_img,yuv420pImg)){
        return false;
    }

#endif

#if ENCODE_BY_MPP
    // encode image
    if(!encodeImageByFFmpeg(raw_img.cols,raw_img.rows,yuv420pImg,encoded_data,meta)){
        return false;
    }
#elif ENCODE_BY_OPENCV

    if(!encodeImageByOpencv(yuv420pImg,encoded_data,meta)){
        return false;
    }
#endif

    return true;
}

bool captureAndEncodeByFFmpeg(const std::string& devNode, std::vector<uchar>& encoded_data, ImageMeta& meta,int total_frames = -1) {

    FFmpegUtils ffmpegUtils(devNode);
    int ret = ffmpegUtils.init(2400, 2000);
    if (ret < 0) {
        std::cerr << "FFmpeg初始化失败" << std::endl;
        return false;
    }

    const int max_retry = 5;        
    const int retry_delay_us = 5000;
    bool last_frame_success = false;
    int frame_idx = 0;

    while(total_frames < 0 || frame_idx < total_frames){
        std::vector<uint8_t> src_frame;
        TimerUtil capTimer;
        capTimer.start();
        ret = ffmpegUtils.capture_frame(src_frame);
        if (ret < 0) {
            std::cerr << "第" << frame_idx << "帧采集失败" << std::endl;
            continue;
        }
        capTimer.end("Capture frame total");
        AVPacket* pkt = nullptr;
        int retry_cnt = 0;
        while (retry_cnt < max_retry) {
            TimerUtil timer;
            timer.start();
            ret = ffmpegUtils.encode(src_frame, &pkt);
      
            if (ret == 0 && pkt != nullptr) {
                timer.end("encode frame total");
                break;
            }
       
            if (ret == AVERROR(EAGAIN)) {
                retry_cnt++;
                usleep(retry_delay_us);
                std::cerr << "第" << frame_idx << "帧编码器忙，重试 " << retry_cnt << "/" << max_retry << std::endl;
                continue;
            }

            std::cerr << "第" << frame_idx << "帧编码错误，错误码：" << ret << std::endl;
            break;
        }

        if (ret == 0 && pkt != nullptr && pkt->size > 0) {
            encoded_data.resize(pkt->size);
            memcpy(encoded_data.data(), pkt->data, pkt->size);

            meta.is_compressed = true;
            meta.comp_data_len = pkt->size;
            meta.rows = 2400;
            meta.cols = 2000;
            frame_idx++;
            last_frame_success = true;

            std::cout << "第" << frame_idx << "帧编码成功，大小：" << pkt->size << " 字节" << std::endl;
        } else {
            std::cerr << "第" << frame_idx << "帧编码最终失败" << std::endl;
            last_frame_success = false;
        }
        if (pkt != nullptr) {
            av_packet_free(&pkt);
        }
    }

    if (last_frame_success) {
        return true;
    } else {
        meta.is_compressed = false;
        meta.comp_data_len = 0;
        return false;
    }

}

bool processByCamera(const std::string& devNode,std::vector<uchar>& encoded_data,ImageMeta& meta){
    // read image by v4l2
     std::vector<uint8_t> raw_img;
    //cv::Mat raw_img;

#if CAPTURE_BY_V4L2
    if(!loadVideoByV4l2(devNode,raw_img)){
        return false;
    }
#elif CAPTURE_BY_OPENCV
    if(!loadVideoByOpencv(devNode,raw_img)){
        return false;
    }
#elif CAPTURE_BY_FFMPEG
    if(!loadVideoByFFmpeg(devNode,raw_img)){
        return false;
    }
#endif


    // encode image
#if ENCODE_BY_MPP
    if(!encodeImageByFFmpeg(2400,2000,raw_img,encoded_data,meta)){
        return false;
    }
#elif ENCODE_BY_OPENCV
    if(!encodeImageByOpencv(raw_img,encoded_data,meta)){
        return false;
    }
#endif
    return true;
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <image_path>" << "<camera node>" << std::endl;
        return -1;
    }

    Logger::setPrintLocation(false);
    Logger::setPrintTime(false);
    
    const char* imgPath = argv[1];
    const char* devNode = argv[2];
//     std::vector<uchar> encoded_data;
//     ImageMeta meta{};
    
//     // process
// #if READ_BY_LOCAL_IMAGE
//     if(!processByImage(imgPath,encoded_data,meta)){
//         return -1;
//     }
// #elif READ_BY_CAMERA
//     if(!processByCamera(devNode,encoded_data,meta)){
//         return -1;
//     }
// #elif READ_BY_FFMPEG
//     if(!captureAndEncodeByFFmpeg(devNode,encoded_data,meta,1)){
//         return -1;
//     }
// #endif


//     // send image
// #if TRANSFER_BY_TCP
//     runTCPServer(encoded_data,meta);
// #elif TRANSFER_BY_ZMQ
//     runZMQServer(encoded_data,meta);
// #endif
    RpcService service(devNode,imgPath);

    if(service.init() < 0) return -1;

    std::cin.get();
    service.close();

    return 0;
}