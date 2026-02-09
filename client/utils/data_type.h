#pragma once

#include <string>
#include <cstdint>
#include <opencv2/opencv.hpp>
#include <msgpack.hpp>

#define COMPRESS_QUALITY  50
#define COMPRESS_FORMAT 1 // 1=JPEG  2=PNG

const std::string SERVER_IP_RK3576 = "192.168.23.36";
const std::string SERVER_IP_RK3588 = "192.168.23.99";
// TCP Port
const int TCP_PORT = 12345;
// ZMQ REP Port
const int ZMQ_PORT = 5565;
// RPC PORT
const int RPC_REP_PORT = 5566;
const int RPC_PUB_PORT = 5567;
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
    MSGPACK_DEFINE(rows, cols,type,is_compressed,compress_type, comp_data_len);
};

struct RpcImageResponse {
    ImageMeta meta;
    std::vector<uint8_t> image_data;
    MSGPACK_DEFINE(meta, image_data);
};

enum class CaptureDevice{
    V4L2,
    OPENCV,
    FFMPEG
};
MSGPACK_ADD_ENUM(CaptureDevice);
enum class EncodeType{
    FFMPEG,
    OPENCV
};
MSGPACK_ADD_ENUM(EncodeType);

enum class CaptureMode{
    SINGLE,
    MULTIPLE
};
MSGPACK_ADD_ENUM(CaptureMode);

struct RpcCMDResponse {
    bool success{false};
    std::string error_msg{};
    RpcImageResponse image_data{};
    MSGPACK_DEFINE(success, error_msg, image_data);
};

struct CameraConfig{
    int width = 2400;
    int height = 2000;
    int fps = 30;
    CaptureDevice cap_device;
    EncodeType encode_type;
    CaptureMode cap_mode;
    MSGPACK_DEFINE(width,height,fps,cap_device,encode_type,cap_mode);
};



