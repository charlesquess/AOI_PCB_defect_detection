#pragma once

#include <string>
#include <memory>
#include <functional>
#include <vector>
#include <cstdint>
#include "utils/types.hpp"

/* 串口通信模块 - 支持 RS232/485，Windows (CreateFile) / Linux (termios) */
class SerialClient {
public:
    explicit SerialClient(const SerialConfig& config);
    ~SerialClient();

    bool open();              /* 打开串口 */
    void close();             /* 关闭串口 */
    bool is_open() const;     /* 串口是否已打开 */

    bool send(const std::vector<uint8_t>& data);  /* 发送二进制数据 */
    bool send(const std::string& message);         /* 发送文本消息 */
    std::vector<uint8_t> receive(size_t timeout_ms = 1000);  /* 接收数据（带超时） */

    /* 串口参数设置 */
    bool set_baudrate(int baudrate);
    bool set_parity(char parity);
    bool set_data_bits(int bits);
    bool set_stop_bits(int bits);

    using DataCallback = std::function<void(const std::vector<uint8_t>&)>;
    void set_receive_callback(DataCallback callback);   /* 设置异步接收回调 */

    SerialConfig config() const;  /* 获取当前配置 */

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
