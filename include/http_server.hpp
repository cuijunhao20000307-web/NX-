#pragma once
#include "app.hpp"
#include <atomic>
#include <string>
#include <thread>

class HttpServer {
public:
    explicit HttpServer(SelectedState& state);
    ~HttpServer();
    bool start(uint16_t port = 8080);
    void stop();
    std::string url() const;

private:
    SelectedState& state_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> failed_{false};
    int listen_fd_ = -1;
    uint16_t port_ = 8080;
    std::string ip_;
    std::thread worker_;
    void run();
    void handle_client(int fd);
};
