#pragma once
#include <chrono>
#include <string>
#include "logger.h"

class TimerUtil
{
public:
    using Clock = std::chrono::steady_clock;

    explicit TimerUtil(bool enable_log = true): m_enableLog(enable_log){ start(); }

    TimerUtil(const TimerUtil&) = delete;
    TimerUtil& operator=(const TimerUtil&) = delete;
    ~TimerUtil() = default;

    void start(){m_startTime = Clock::now();}

    double end(const std::string& tag = "代码块")
    {
        m_endTime = Clock::now();
        double cost_ms = getDurationMs(m_startTime, m_endTime);

        if (m_enableLog) {
            printTimeCost(tag, cost_ms);
        }

        return cost_ms;
    }

    double getCostMs() const
    {
        auto now = Clock::now();
        return getDurationMs(m_startTime, now);
    }

    void reset(){ start();}
    void setEnableLog(bool enable) { m_enableLog = enable;}
    bool isLogEnabled() const {return m_enableLog;}

private:
    Clock::time_point m_startTime;
    Clock::time_point m_endTime;
    bool m_enableLog{true};

    static double getDurationMs(const Clock::time_point& start,
                                const Clock::time_point& end)
    {
        return std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(end - start).count();
    }

    void printTimeCost(const std::string& tag, double cost_ms) const
    {
        if (cost_ms >= 1000.0) {
            LOG_DEBUG("[%s] cost: %.3f s", tag.c_str(), cost_ms / 1000.0);
        } 
        else if (cost_ms >= 1.0) {
            LOG_DEBUG("[%s] cost: %.3f ms", tag.c_str(), cost_ms);
        } 
        else if (cost_ms >= 0.001) {
            LOG_DEBUG("[%s] cost: %.3f us", tag.c_str(), cost_ms * 1000.0);
        } 
        else {
            LOG_DEBUG("[%s] cost: %.3f ns", tag.c_str(), cost_ms * 1e6);
        }
    }
};
