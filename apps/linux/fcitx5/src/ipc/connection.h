//! 有界 Unix socket 传输；任何错误都关闭连接，下一次按键重新握手。
#pragma once
#include <nlohmann/json.hpp>
namespace qingjian {
class Connection {
public:
    Connection() = default;
    Connection(const Connection &) = delete;
    Connection &operator=(const Connection &) = delete;
    ~Connection();
    bool open();
    bool send(const nlohmann::json &message, nlohmann::json *response = nullptr);
    bool connected() const { return fd_ >= 0; }
    void close();
private:
    /// 当前连接；-1 表示未连接。
    int fd_ = -1;
};
}
