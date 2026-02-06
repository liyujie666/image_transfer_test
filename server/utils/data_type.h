#pragma once

#include <string>
#include <cstdint>
#include <opencv2/opencv.hpp>

#define COMPRESS_QUALITY  50
#define COMPRESS_FORMAT 1 // 1=JPEG  2=PNG

const std::string SERVER_IP = "192.168.23.36";
// TCP Port
const int TCP_PORT = 12345;
// ZMQ REP Port
const int ZMQ_PORT = 5555;

// client reques
struct Request {
    uint32_t seq;
};

struct ImageMeta {
    int rows = 0;        
    int cols = 0;        
    int type = 0;        
    bool is_compressed; 
    int compress_type;   // 压缩格式：1=JPEG  2=PNG
    size_t comp_data_len;
};




