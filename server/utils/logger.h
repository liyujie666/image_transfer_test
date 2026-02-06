// logger.h
#pragma once
#include <cstdio>
#include <cstdarg>
#include <mutex>
#include <cstring>
#include <ctime>

// 日志等级
enum class LogLevel { DEBUG = 0, INFO, WARN, ERROR, FATAL };

class Logger {
public:
    // 输出到文件+控制台；传 nullptr 则只控制台
    static void init(const char* logFile = nullptr) {
        std::lock_guard<std::mutex> lk(mtx_);
        if (logFile) {
            fp_ = std::fopen(logFile, "a");
            if (!fp_) std::fprintf(stderr, "[Logger] fopen %s failed\n", logFile);
        }
    }

    // 设置是否打印时间戳
    static void setPrintTime(bool print) {
        std::lock_guard<std::mutex> lk(mtx_);
        printTime_ = print;
    }

    // 设置是否打印函数位置（类名、函数名、行号）
    static void setPrintLocation(bool print) {
        std::lock_guard<std::mutex> lk(mtx_);
        printLocation_ = print;
    }

    // 核心打印函数
    static void log(LogLevel level,
                    const char* cls,
                    const char* func,
                    int line,
                    const char* fmt, ...) {
        std::lock_guard<std::mutex> lk(mtx_);

        const char* levelStr[] = { "DEBUG", "INFO ", "WARN ", "ERROR", "FATAL" };
        char prefix[256] = {0};  // 用于构建前缀
        int prefixLen = 0;

        // 时间戳
        char timeStr[32] = {0};
        if (printTime_) {
            time_t now = time(nullptr);
            struct tm tmbuf{};
            localtime_r(&now, &tmbuf);
            std::strftime(timeStr, sizeof(timeStr), "%m-%d %H:%M:%S", &tmbuf);
        }

        // 构建前缀：根据配置拼接各部分
        if (printTime_) {
            prefixLen += std::snprintf(prefix + prefixLen, sizeof(prefix) - prefixLen,
                                     "[%s]", timeStr);
        }

        // 始终打印日志等级
        prefixLen += std::snprintf(prefix + prefixLen, sizeof(prefix) - prefixLen,
                                 "[%s]", levelStr[static_cast<int>(level)]);

        // 函数位置信息
        if (printLocation_) {
            prefixLen += std::snprintf(prefix + prefixLen, sizeof(prefix) - prefixLen,
                                     "[%s::%s:%d]", cls, func, line);
        }

        // 前缀末尾添加空格分隔
        if (prefixLen > 0) {
            prefixLen += std::snprintf(prefix + prefixLen, sizeof(prefix) - prefixLen, " ");
        }

        // 打印前缀
        std::printf("%s", prefix);
        if (fp_) std::fprintf(fp_, "%s", prefix);

        // 可变参数正文
        va_list args;
        va_start(args, fmt);
        std::vprintf(fmt, args);
        if (fp_) std::vfprintf(fp_, fmt, args);
        va_end(args);

        // 换行并刷新
        std::putchar('\n');
        if (fp_) std::putc('\n', fp_);
        std::fflush(stdout);
        if (fp_) std::fflush(fp_);
    }

    static void close() {
        std::lock_guard<std::mutex> lk(mtx_);
        if (fp_) { std::fclose(fp_); fp_ = nullptr; }
    }

private:
    static FILE* fp_;
    static std::mutex mtx_;
    static bool printTime_;       // 控制是否打印时间戳
    static bool printLocation_;   // 控制是否打印函数位置信息
};

#define LOG_DEBUG(...)  Logger::log(LogLevel::DEBUG, __FUNCTION__, __func__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...)   Logger::log(LogLevel::INFO,  __FUNCTION__, __func__, __LINE__, __VA_ARGS__)
#define LOG_WARN(...)   Logger::log(LogLevel::WARN,  __FUNCTION__, __func__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...)  Logger::log(LogLevel::ERROR, __FUNCTION__, __func__, __LINE__, __VA_ARGS__)
#define LOG_FATAL(...)  Logger::log(LogLevel::FATAL, __FUNCTION__, __func__, __LINE__, __VA_ARGS__)