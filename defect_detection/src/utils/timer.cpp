#include "utils/timer.hpp"
#include <iostream>

Timer::Timer() : running_(false) {}
Timer::Timer(const std::string& label) : label_(label), running_(false) { start(); }
Timer::~Timer() = default;

/* 开始计时 */
void Timer::start() {
    running_ = true;
    start_ = std::chrono::high_resolution_clock::now();
}

/* 停止计时 */
void Timer::stop() {
    end_ = std::chrono::high_resolution_clock::now();
    running_ = false;
}

/* 重置计时器 */
void Timer::reset() {
    running_ = false;
}

/* 返回已过毫秒数；如果仍在运行则返回当前已过时间 */
double Timer::elapsed_ms() const {
    auto end = running_ ? std::chrono::high_resolution_clock::now() : end_;
    return std::chrono::duration<double, std::milli>(end - start_).count();
}

/* 返回已过微秒数 */
double Timer::elapsed_us() const {
    auto end = running_ ? std::chrono::high_resolution_clock::now() : end_;
    return std::chrono::duration<double, std::micro>(end - start_).count();
}

/* 返回已过秒数 */
double Timer::elapsed_s() const {
    auto end = running_ ? std::chrono::high_resolution_clock::now() : end_;
    return std::chrono::duration<double>(end - start_).count();
}

/* 打印耗时到控制台 */
void Timer::print_elapsed(const std::string& prefix) const {
    std::string tag = prefix.empty() ? label_ : prefix;
    std::cout << tag << ": " << elapsed_ms() << " ms" << std::endl;
}
