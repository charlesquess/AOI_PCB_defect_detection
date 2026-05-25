#include "communication/serial_client.hpp"
#include "utils/logger.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <termios.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/select.h>
#endif

class SerialClient::Impl {
public:
    explicit Impl(const SerialConfig& cfg) : config_(cfg), fd_(-1) {}

    bool open() {
        close();
        LOG_INFO("正在打开串口: " + config_.port);

#ifdef _WIN32
        std::string target = "\\\\.\\" + config_.port;
        HANDLE h = CreateFileA(
            target.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, nullptr
        );
        if (h == INVALID_HANDLE_VALUE) {
            LOG_ERROR("打开串口失败: " + config_.port + " (错误码 " + std::to_string(GetLastError()) + ")");
            return false;
        }
        fd_ = reinterpret_cast<intptr_t>(h);

        // 配置串口参数
        DCB dcb = {0};
        dcb.DCBlength = sizeof(DCB);
        if (!GetCommState(h, &dcb)) {
            LOG_ERROR("获取串口状态失败");
            close();
            return false;
        }

        dcb.BaudRate = config_.baudrate;
        dcb.fBinary = TRUE;
        dcb.fParity = FALSE;
        dcb.ByteSize = config_.data_bits;
        dcb.StopBits = (config_.stop_bits == 2) ? TWOSTOPBITS : ONESTOPBIT;

        switch (toupper(config_.parity)) {
            case 'E': dcb.Parity = EVENPARITY; dcb.fParity = TRUE; break;
            case 'O': dcb.Parity = ODDPARITY;  dcb.fParity = TRUE; break;
            default:  dcb.Parity = NOPARITY;   dcb.fParity = FALSE; break;
        }

        if (!SetCommState(h, &dcb)) {
            LOG_ERROR("设置串口参数失败 (错误码 " + std::to_string(GetLastError()) + ")");
            close();
            return false;
        }

        // 设置超时
        COMMTIMEOUTS timeouts = {0};
        timeouts.ReadIntervalTimeout = 50;
        timeouts.ReadTotalTimeoutConstant = 100;
        timeouts.ReadTotalTimeoutMultiplier = 10;
        timeouts.WriteTotalTimeoutConstant = 100;
        timeouts.WriteTotalTimeoutMultiplier = 10;
        SetCommTimeouts(h, &timeouts);

        LOG_INFO("串口 " + config_.port + " 已打开 (波特率 " + std::to_string(config_.baudrate) + ")");
        return true;

#else
        // Linux: open + tcsetattr
        fd_ = ::open(config_.port.c_str(), O_RDWR | O_NOCTTY);
        if (fd_ < 0) {
            LOG_ERROR("打开串口失败: " + config_.port);
            return false;
        }
        struct termios tty = {0};
        if (tcgetattr(fd_, &tty) != 0) {
            close();
            return false;
        }
        cfsetospeed(&tty, config_.baudrate);
        cfsetispeed(&tty, config_.baudrate);
        tty.c_cflag |= (CLOCAL | CREAD);
        tty.c_cflag &= ~CSIZE;
        tty.c_cflag |= CS8;
        tty.c_cflag &= ~PARENB;
        tty.c_iflag &= ~(IXON | IXOFF | IXANY);
        tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
        tty.c_oflag &= ~OPOST;
        tty.c_cc[VMIN] = 0;
        tty.c_cc[VTIME] = 10;
        tcsetattr(fd_, TCSANOW, &tty);
        LOG_INFO("串口 " + config_.port + " 已打开");
        return true;
#endif
    }

    void close() {
#ifdef _WIN32
        if (fd_ >= 0) {
            CloseHandle(reinterpret_cast<HANDLE>(fd_));
            fd_ = -1;
        }
#else
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
#endif
    }

    bool is_open() const { return fd_ >= 0; }

    bool send(const std::vector<uint8_t>& data) {
        if (fd_ < 0) return false;
#ifdef _WIN32
        DWORD written = 0;
        return WriteFile(reinterpret_cast<HANDLE>(fd_),
                         data.data(), (DWORD)data.size(), &written, nullptr);
#else
        return ::write(fd_, data.data(), data.size()) > 0;
#endif
    }

    bool send(const std::string& message) {
        std::vector<uint8_t> data(message.begin(), message.end());
        return send(data);
    }

    std::vector<uint8_t> receive(size_t timeout_ms) {
        if (fd_ < 0) return {};
#ifdef _WIN32
        HANDLE h = reinterpret_cast<HANDLE>(fd_);
        DWORD errors;
        COMSTAT status;
        ClearCommError(h, &errors, &status);
        if (status.cbInQue == 0) return {};

        std::vector<uint8_t> buf(status.cbInQue);
        DWORD read = 0;
        if (ReadFile(h, buf.data(), status.cbInQue, &read, nullptr)) {
            buf.resize(read);
            return buf;
        }
#else
        struct timeval tv = {(long)(timeout_ms / 1000), (long)((timeout_ms % 1000) * 1000)};
        fd_set set;
        FD_ZERO(&set);
        FD_SET(fd_, &set);
        if (select(fd_ + 1, &set, nullptr, nullptr, &tv) <= 0) return {};

        uint8_t buf[1024];
        int n = ::read(fd_, buf, sizeof(buf));
        if (n > 0) return std::vector<uint8_t>(buf, buf + n);
#endif
        return {};
    }

    bool set_baudrate(int baudrate) {
        config_.baudrate = baudrate;
        // 实际生效需 reopen
        return true;
    }

    bool set_parity(char parity) {
        config_.parity = parity;
        return true;
    }

    bool set_data_bits(int bits) {
        config_.data_bits = bits;
        return true;
    }

    bool set_stop_bits(int bits) {
        config_.stop_bits = bits;
        return true;
    }

    void set_receive_callback(DataCallback cb) {
        callback_ = cb;
    }

    SerialConfig config() const { return config_; }

private:
    SerialConfig config_;
    intptr_t fd_;  // Windows: HANDLE; Linux: fd
    DataCallback callback_;
};

SerialClient::SerialClient(const SerialConfig& config)
    : impl_(std::make_unique<Impl>(config)) {}
SerialClient::~SerialClient() = default;
bool SerialClient::open() { return impl_->open(); }
void SerialClient::close() { impl_->close(); }
bool SerialClient::is_open() const { return impl_->is_open(); }
bool SerialClient::send(const std::vector<uint8_t>& d) { return impl_->send(d); }
bool SerialClient::send(const std::string& m) { return impl_->send(m); }
std::vector<uint8_t> SerialClient::receive(size_t t) { return impl_->receive(t); }
bool SerialClient::set_baudrate(int b) { return impl_->set_baudrate(b); }
bool SerialClient::set_parity(char p) { return impl_->set_parity(p); }
bool SerialClient::set_data_bits(int b) { return impl_->set_data_bits(b); }
bool SerialClient::set_stop_bits(int b) { return impl_->set_stop_bits(b); }
void SerialClient::set_receive_callback(DataCallback cb) { impl_->set_receive_callback(cb); }
SerialConfig SerialClient::config() const { return impl_->config(); }
