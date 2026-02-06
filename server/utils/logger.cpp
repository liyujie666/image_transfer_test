#include "logger.h"

FILE* Logger::fp_ = nullptr;
std::mutex Logger::mtx_;
bool Logger::printTime_ = true;       // 默认打印时间
bool Logger::printLocation_ = true;   // 默认打印函数位置