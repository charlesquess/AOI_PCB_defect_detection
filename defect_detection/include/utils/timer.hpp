#pragma once

#include <chrono>
#include <string>

/* 高性能计时器 - 用于代码段耗时测量 */
class Timer {
public:
    Timer();
    explicit Timer(const std::string& label);  /* 带标签的计时器 */
    ~Timer();

    void start();  /* 开始计时 */
    void stop();   /* 停止计时 */
    void reset();  /* 重置计时器 */

    double elapsed_ms() const;  /* 获取已过毫秒数 */
    double elapsed_us() const;  /* 获取已过微秒数 */
    double elapsed_s() const;   /* 获取已过秒数 */

    void print_elapsed(const std::string& prefix = "") const;  /* 打印耗时到控制台 */

private:
    std::string label_;                                              /* 标签 */
    std::chrono::high_resolution_clock::time_point start_;          /* 开始时间点 */
    std::chrono::high_resolution_clock::time_point end_;            /* 结束时间点 */
    bool running_;                                                   /* 是否正在运行 */
};
