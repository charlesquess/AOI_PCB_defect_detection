#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <opencv2/core/mat.hpp>
#include "types.hpp"

/* 线程安全的任务帧 */
struct FrameTask {
    int frame_id = 0;
    uint64_t timestamp_us = 0;    /* 采集时间戳 */
    cv::Mat frame;                 /* 原图 */
    cv::Mat processed;             /* 预处理后（推理用） */
    std::vector<DetectionResult> results;
    bool has_result = false;       /* 推理是否完成 */
};

/* 线程安全有界队列 — 生产者/消费者模式 */
template<typename T>
class ThreadSafeQueue {
public:
    explicit ThreadSafeQueue(size_t max_size = 5)
        : max_size_(max_size), closed_(false) {}

    /* 生产者：推送数据，队列满时阻塞 */
    bool push(T item, int timeout_ms = 100) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (closed_) return false;
        if (!cv_space_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                                [this] { return queue_.size() < max_size_ || closed_; })) {
            return false;  // 超时
        }
        if (closed_) return false;
        queue_.push(std::move(item));
        cv_data_.notify_one();
        return true;
    }

    /* 消费者：取出数据，队列空时阻塞 */
    bool pop(T& item, int timeout_ms = 100) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!cv_data_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                               [this] { return !queue_.empty() || closed_; })) {
            return false;  // 超时
        }
        if (queue_.empty()) return false;
        item = std::move(queue_.front());
        queue_.pop();
        cv_space_.notify_one();
        return true;
    }

    /* 取最新数据：丢弃队列中所有旧数据，只返回最新的 */
    bool pop_latest(T& item, int timeout_ms = 100) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!cv_data_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                               [this] { return !queue_.empty() || closed_; })) {
            return false;
        }
        // 弹出所有旧数据，只保留最新的
        while (!queue_.empty()) {
            item = std::move(queue_.front());
            queue_.pop();
        }
        cv_space_.notify_all();  // 队列空了，通知生产者
        return true;
    }

    /* 关闭队列，唤醒所有等待线程 */
    void close() {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
        cv_data_.notify_all();
        cv_space_.notify_all();
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

private:
    size_t max_size_;
    bool closed_;
    std::queue<T> queue_;
    mutable std::mutex mutex_;
    std::condition_variable cv_data_;
    std::condition_variable cv_space_;
};
