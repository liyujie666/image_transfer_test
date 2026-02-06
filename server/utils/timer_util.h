#pragma once
#include <iostream>
#include <chrono>
#include <string>

class TimerUtil
{
public:
    TimerUtil() { 
        //start(); 
    }
    ~TimerUtil() = default;
    TimerUtil(const TimerUtil&) = delete;
    TimerUtil& operator=(const TimerUtil&) = delete;

    void start()
    {
        m_startTime = std::chrono::high_resolution_clock::now();
    }

    void end(const std::string& tag = "目标代码块")
    {
        m_endTime = std::chrono::high_resolution_clock::now();
        printTimeCost(tag);
    }

    void reset()
    {
        start();
    }

    double getCostMs() const
    {
        auto currentTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(currentTime - m_startTime);
        return duration.count() / 1000.0;
    }

private:
    std::chrono::high_resolution_clock::time_point m_startTime;
    std::chrono::high_resolution_clock::time_point m_endTime;

    void printTimeCost(const std::string& tag) const
    {
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(m_endTime - m_startTime).count();
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(m_endTime - m_startTime).count();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(m_endTime - m_startTime).count();
        auto s  = std::chrono::duration_cast<std::chrono::seconds>(m_endTime - m_startTime).count();

        if (s > 0)
            std::cout << "[" << tag << "] cost: " << s << "s (" << ms << "ms)\n";
        else if (ms > 0)
            std::cout << "[" << tag << "] cost: " << ms << "ms (" << us << "us)\n";
        else if (us > 0)
            std::cout << "[" << tag << "] cost: " << us << "us (" << ns << "ns)\n";
        else
            std::cout << "[" << tag << "] cost: " << ns << "ns\n";
    }
};