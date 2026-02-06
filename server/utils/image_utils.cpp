#include "image_utils.h"
#include "timer_util.h"
#include "ffmpeg_utils.h"
#include "v4l2_capture.h"
#include "im2d.h"
#include "rga.h"
#include <iostream>
#include <cstring>

extern"C"{
#include "libavutil/hwcontext_drm.h"
#include <drm/drm_fourcc.h>
}
bool loadImageByOpencv(const std::string& imagePath, cv::Mat& img)
{
    TimerUtil timer;
    timer.start();

    img = cv::imread(imagePath, cv::IMREAD_COLOR);
    if (img.empty()) {
        std::cerr << "Failed to read image: " << imagePath << std::endl;
        return false;
    }

    timer.end("Read image by opencv");

    std::cout << "Image loaded: "
              << img.cols << "x" << img.rows
              << " type=" << img.type() << std::endl;

    return true;
}

bool loadVideoByOpencv(const std::string& devNode, cv::Mat& srcImg)
{
    TimerUtil timer;
    cv::VideoCapture cap(devNode, cv::CAP_V4L2);
    if (!cap.isOpened()) {
        std::cerr << "Failed to open video device: " << devNode << std::endl;
        return false;
    }

    cap.set(cv::CAP_PROP_FRAME_WIDTH,  2400);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, 2000);

    // // 读取一帧
    // cap >> srcImg;
    // if (srcImg.empty()) {
    //     std::cerr << "Failed to capture frame from: " << devNode << std::endl;
    //     return false;
    // }
    cv::Mat frame;
    for (int i = 0; i < 10; ++i) {
        timer.start();
        cap >> frame;
        if (frame.empty()) {
            std::cerr << "Failed to capture frame " << i << " from: " << devNode << std::endl;
            return false;
        }
        timer.end("Capture frame by opencv");
    }

    srcImg = std::move(frame);

    std::cout << "Frame captured: "
              << srcImg.cols << "x" << srcImg.rows
              << " type=" << srcImg.type() << std::endl;

    return true;
}

bool loadVideoByV4l2(const std::string& devNode,cv::Mat& srcImg){

    // init v4l2 capture
    TimerUtil captureTimer;
    V4L2Capture capture(devNode);
    if(!capture.init()){
        std::cerr << "v4l2 capture init failed" << std::endl;
        return false;
    }

    // // capture one frame
    // captureTimer.start();
    // if(!capture.captureFrame(srcImg)){
    //     std::cerr << "v4l2 capture capture frame failed" << std::endl;
    //     return false;
    // }
    // captureTimer.end("Read image by v4l2");

    // 连续读取 10 帧
    for (int i = 0; i < 10; ++i) {
        cv::Mat frame;
        captureTimer.start();
        if (!capture.captureFrame(frame)) {
            std::cerr << "v4l2 capture frame " << i << " failed" << std::endl;
            return false;
        }
        captureTimer.end("Read image by v4l2");

        srcImg = std::move(frame);
    }
    
    return true;
}

bool loadVideoByV4l2(const std::string& devNode,std::vector<uint8_t>& srcImg){

    // init v4l2 capture
    TimerUtil captureTimer;
    V4L2Capture capture(devNode);
    if(!capture.init()){
        std::cerr << "v4l2 capture init failed" << std::endl;
        return false;
    }

    // // capture one frame
    // captureTimer.start();
    // if(!capture.captureFrame(srcImg)){
    //     std::cerr << "v4l2 capture capture frame failed" << std::endl;
    //     return false;
    // }
    // captureTimer.end("Read image by v4l2");

    // 连续读取 10 帧
    for (int i = 0; i < 10; ++i) {
        std::vector<uint8_t> frame;
        captureTimer.start();
        if (!capture.captureFrame(frame)) {
            std::cerr << "v4l2 capture frame " << i << " failed" << std::endl;
            return false;
        }
        captureTimer.end("Read image by v4l2");

        srcImg = std::move(frame);
    }
    
    return true;
}

bool loadVideoByFFmpeg(const std::string& devNode,cv::Mat& srcImg){
    return true;
}
bool loadVideoByFFmpeg(const std::string& devNode,std::vector<uint8_t>& srcImg){
    TimerUtil capTimer;
    FFmpegUtils ffmCapture(devNode);

    int ret = ffmCapture.init_capture(2400,2000);
    if(ret < 0){
        std::cerr << "ffmpeg capturer init failed" << std::endl;
        return false;
    }
    capTimer.start();
    ret = ffmCapture.capture_frame(srcImg);
    if(ret < 0){
        std::cerr << "ffmpeg capture frame failed" << std::endl;
        return false;
    }
    capTimer.end("capture by ffmpeg");
    return true;
}

bool convertBgrToYuv420pByCv(const cv::Mat& bgr, cv::Mat& yuv420)
{
    if (bgr.empty()) {
        std::cerr << "BGR image empty" << std::endl;
        return false;
    }

    if (bgr.type() != CV_8UC3) {
        std::cerr << "Input must be CV_8UC3 (BGR)" << std::endl;
        return false;
    }

    TimerUtil timer;
    timer.start();

    cv::cvtColor(bgr, yuv420, cv::COLOR_BGR2YUV_I420);

    timer.end("convert image");

    return yuv420.isContinuous();
}

bool convertBgrToYuv420pByRga(const cv::Mat& bgr, cv::Mat& yuv420){
if (bgr.empty() || bgr.type() != CV_8UC3) {
        std::cerr << "Invalid BGR input" << std::endl;
        return false;
    }

    TimerUtil timer;
    timer.start();

    int width  = bgr.cols;
    int height = bgr.rows;

    // YUV420P 的 Mat：rows = h * 3 / 2
    yuv420.create(height * 3 / 2, width, CV_8UC1);

    rga_buffer_t src = wrapbuffer_virtualaddr(
        (void*)bgr.data,
        width,
        height,
        RK_FORMAT_BGR_888
    );

    rga_buffer_t dst = wrapbuffer_virtualaddr(
        (void*)yuv420.data,
        width,
        height,
        RK_FORMAT_YCbCr_420_P   // I420
    );
    timer.end("rga_buffer_t");
    timer.reset();
    int ret = imcvtcolor(src, dst, RK_FORMAT_BGR_888, RK_FORMAT_YCbCr_420_P);
    if (ret != IM_STATUS_SUCCESS) {
        std::cerr << "RGA BGR->YUV420P failed: " << ret << std::endl;
        return false;
    }

    timer.end("convet image");
    return true;

}

bool convertBgrToNV12ByCv(const cv::Mat& bgr, cv::Mat& nv12){
    if (bgr.empty()) {
        std::cerr << "BGR image empty" << std::endl;
        return false;
    }

    if (bgr.type() != CV_8UC3) {
        std::cerr << "Input must be CV_8UC3 (BGR)" << std::endl;
        return false;
    }

    return true;
}

bool convertBgrToNV12ByRga(const cv::Mat& bgr, cv::Mat& nv12){
    if (bgr.empty() || bgr.type() != CV_8UC3) {
        std::cerr << "Invalid BGR input" << std::endl;
        return false;
    }

    TimerUtil timer;
    timer.start();

    int width  = bgr.cols;
    int height = bgr.rows;

    nv12.create(height * 3 / 2, width, CV_8UC1);

    rga_buffer_t src = wrapbuffer_virtualaddr(
        (void*)bgr.data,
        width,
        height,
        RK_FORMAT_BGR_888
    );

    rga_buffer_t dst = wrapbuffer_virtualaddr(
        (void*)nv12.data,
        width,
        height,
        RK_FORMAT_YCbCr_420_SP   // NV12
    );
    timer.end("rga_buffer_t");
    timer.reset();
    int ret = imcvtcolor(src, dst, RK_FORMAT_BGR_888, RK_FORMAT_YCbCr_420_SP);
    if (ret != IM_STATUS_SUCCESS) {
        std::cerr << "RGA BGR->NV12 failed: " << ret << std::endl;
        return false;
    }

    timer.end("convet image");
    
    return true;
}

bool convertNv12ToBgrByRga(const cv::Mat& nv12,cv::Mat& bgr){
    if (nv12.empty() || nv12.type() != CV_8UC1) {
        std::cerr << "Invalid NV12 input: 必须是单通道8位灰度Mat" << std::endl;
        return false;
    }

    int width = nv12.cols;
    int height = nv12.rows * 2 / 3; 
    if (nv12.rows != height * 3 / 2) {
        std::cerr << "Invalid NV12 size: 高度必须是原始高度的1.5倍（当前NV12高度：" 
                  << nv12.rows << "，计算原始高度：" << height << "）" << std::endl;
        return false;
    }

    TimerUtil timer;
    timer.start();

    // 初始化输出BGR Mat
    bgr.create(height, width, CV_8UC3);
    if (bgr.empty()) {
        std::cerr << "Create BGR Mat failed" << std::endl;
        return false;
    }

    rga_buffer_t src = wrapbuffer_virtualaddr(
        (void*)nv12.data,    // NV12原始数据地址
        width,               // 图像宽度
        height,              // 图像高度（原始高度，非NV12的1.5倍高度）
        RK_FORMAT_YCbCr_420_SP  // NV12对应的RGA格式
    );

    rga_buffer_t dst = wrapbuffer_virtualaddr(
        (void*)bgr.data,     // BGR输出数据地址
        width,               // 图像宽度
        height,              // 图像高度
        RK_FORMAT_BGR_888    // BGR对应的RGA格式
    );
    timer.end("RGA buffer wrap");

    timer.reset();
    int ret = imcvtcolor(src, dst, RK_FORMAT_YCbCr_420_SP, RK_FORMAT_BGR_888);
    if (ret != IM_STATUS_SUCCESS) {
        std::cerr << "RGA NV12->BGR failed: ret=" << ret << std::endl;
        bgr.release();  // 失败时释放输出Mat
        return false;
    }
    timer.end("RGA NV12 to BGR convert");

    return true;
}


bool encodeImageByFFmpeg(const int& srcWidth,const int& srcHeight,const cv::Mat& srcImg,std::vector<uchar>& outData,ImageMeta& meta)
{
    if (srcImg.empty()) return false;

    meta.cols = srcWidth;
    meta.rows = srcHeight;
    meta.type = srcImg.type();

    // init ffmpeg encoder
    FFmpegUtils codec;
    CodecParams params;
    params.width = srcWidth;
    params.height = srcHeight; 
    params.pixfmt = AV_PIX_FMT_NV12;      
    params.codec_type = CodecType::MJPEG_RKMPP;

    int ret = codec.init_encoder(params);
    if (ret < 0) {
        std::cerr << "FFmpeg encoder init failed" << std::endl;
        return false;
    }

    TimerUtil timer;
    timer.start();

    // encode
    AVPacket* pkt = nullptr;
    ret = codec.encode(srcImg,&pkt);
    if (ret < 0) {
        std::cerr << "FFmpeg encode failed" << std::endl;
        return false;
    }

    timer.end("FFmpeg encode");

    // copy data to vector
    if (pkt && pkt->size > 0) {
        outData.resize(pkt->size);
        memcpy(outData.data(), pkt->data, pkt->size);
        std::cout << "pkt size " << pkt->size << std::endl;
        meta.is_compressed = true;
        meta.comp_data_len = pkt->size;

        av_packet_free(&pkt);
        return true;
    }

    meta.is_compressed = false;
    return false;
}


bool encodeImageByFFmpeg(const int& srcWidth,const int& srcHeight,const std::vector<uint8_t>& srcImg,std::vector<uchar>& outData,ImageMeta& meta){
    if (srcImg.empty()) return false;

    meta.cols = srcWidth;
    meta.rows = srcHeight;

    // init ffmpeg encoder
    FFmpegUtils codec;
    CodecParams params;
    params.width = srcWidth;
    params.height = srcHeight; 
    params.pixfmt = AV_PIX_FMT_NV12;      
    params.codec_type = CodecType::MJPEG_RKMPP;

    int ret = codec.init_encoder(params);
    if (ret < 0) {
        std::cerr << "FFmpeg encoder init failed" << std::endl;
        return false;
    }

    TimerUtil timer;
    timer.start();

    // encode
    AVPacket* pkt = nullptr;
    ret = codec.encode(srcImg,&pkt);
    if (ret < 0) {
        std::cerr << "FFmpeg encode failed" << std::endl;
        return false;
    }

    timer.end("FFmpeg encode");

    // copy data to vector
    if (pkt && pkt->size > 0) {
        outData.resize(pkt->size);
        memcpy(outData.data(), pkt->data, pkt->size);
        std::cout << "pkt size " << pkt->size << std::endl;
        meta.is_compressed = true;
        meta.comp_data_len = pkt->size;

        av_packet_free(&pkt);
        return true;
    }

    meta.is_compressed = false;
    return false;
}


bool encodeImageByOpencv(const cv::Mat& srcImg,std::vector<uchar>& outData,ImageMeta& meta){
    TimerUtil encodeTimer;
    encodeTimer.start();

    cv::Mat encode_img = srcImg.clone();
    std::vector<int> encode_params;
    if(COMPRESS_FORMAT == 1) {
        encode_params = {cv::IMWRITE_JPEG_QUALITY, COMPRESS_QUALITY};
    } else {
        encode_params = {cv::IMWRITE_PNG_COMPRESSION, COMPRESS_QUALITY/10}; 
    }
    bool ret = cv::imencode(COMPRESS_FORMAT==1 ? ".jpg" : ".png", encode_img, outData, encode_params);
    if(ret) {
        encodeTimer.end("cv::imencode");
        meta.is_compressed = true;
        meta.compress_type = COMPRESS_FORMAT;
        meta.comp_data_len = outData.size();
        std::cout << "Compressed Image total size: " << outData.size() / 1024 << "KB" << std::endl;
    }
    return ret;
}
