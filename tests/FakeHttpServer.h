#pragma once

// A tiny HTTP server on 127.0.0.1 for the network tests: one request per connection, scripted answers.
// POSIX only (macOS and Linux CI); the network layer itself is what is under test, so no mock sits in between.

#include <arpa/inet.h>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <netinet/in.h>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace mm::test {

struct FakeRequest {
    std::string method;
    std::string path;
    std::map<std::string, std::string> headers; ///< names in lower case
    std::string body;
};

struct FakeReply {
    int status = 200;
    std::string body;
    std::map<std::string, std::string> headers;
    int delayMs = 0;       ///< waits before it answers (the connection stays open)
    bool hangUp = false;   ///< closes the connection without an answer
    int partialBytes = -1; ///< >= 0: sends the head and only this many body bytes, then waits `delayMs`, then stops
};

class FakeHttpServer {
public:
    using Handler = std::function<FakeReply(const FakeRequest&)>;

    explicit FakeHttpServer(Handler handler) : handler_(std::move(handler)) {
        listener_ = ::socket(AF_INET, SOCK_STREAM, 0);
        int yes = 1;
        ::setsockopt(listener_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        ::bind(listener_, reinterpret_cast<sockaddr*>(&address), sizeof(address));
        socklen_t length = sizeof(address);
        ::getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &length);
        port_ = ntohs(address.sin_port);
        ::listen(listener_, 16);
        thread_ = std::thread([this] { accept(); });
    }

    ~FakeHttpServer() {
        stop_.store(true);
        thread_.join();
        for (auto& worker : workers_) {
            worker.join();
        }
        ::close(listener_);
    }

    int port() const { return port_; }
    std::string url(const std::string& path = "") const { return "http://127.0.0.1:" + std::to_string(port_) + path; }

    std::vector<FakeRequest> requests() const {
        const std::lock_guard<std::mutex> lock(mutex_);
        return requests_;
    }

private:
    void accept() {
        while (!stop_.load()) {
            pollfd descriptor{listener_, POLLIN, 0};
            if (::poll(&descriptor, 1, 20) <= 0) {
                continue;
            }
            const int client = ::accept(listener_, nullptr, nullptr);
            if (client >= 0) {
                workers_.emplace_back([this, client] { serve(client); });
            }
        }
    }

    static bool readUntilHeaderEnd(int client, std::string& data, std::atomic<bool>& stop) {
        char buffer[4096];
        while (data.find("\r\n\r\n") == std::string::npos) {
            pollfd descriptor{client, POLLIN, 0};
            if (stop.load()) {
                return false;
            }
            if (::poll(&descriptor, 1, 20) <= 0) {
                continue;
            }
            const auto count = ::recv(client, buffer, sizeof(buffer), 0);
            if (count <= 0) {
                return false;
            }
            data.append(buffer, static_cast<size_t>(count));
        }
        return true;
    }

    void serve(int client) {
        std::string data;
        FakeRequest request;
        if (readUntilHeaderEnd(client, data, stop_)) {
            const auto headerEnd = data.find("\r\n\r\n");
            const std::string head = data.substr(0, headerEnd);
            std::string rest = data.substr(headerEnd + 4);
            size_t lineEnd = head.find("\r\n");
            const std::string first = head.substr(0, lineEnd);
            const auto space1 = first.find(' ');
            const auto space2 = first.find(' ', space1 + 1);
            request.method = first.substr(0, space1);
            request.path = first.substr(space1 + 1, space2 - space1 - 1);
            while (lineEnd != std::string::npos) {
                const size_t start = lineEnd + 2;
                lineEnd = head.find("\r\n", start);
                const std::string line =
                    head.substr(start, lineEnd == std::string::npos ? std::string::npos : lineEnd - start);
                const auto colon = line.find(':');
                if (colon == std::string::npos) {
                    continue;
                }
                std::string name = line.substr(0, colon);
                for (auto& c : name) {
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                }
                size_t valueStart = colon + 1;
                while (valueStart < line.size() && line[valueStart] == ' ') {
                    ++valueStart;
                }
                request.headers[name] = line.substr(valueStart);
            }
            size_t length = 0;
            if (const auto it = request.headers.find("content-length"); it != request.headers.end()) {
                length = static_cast<size_t>(std::stoul(it->second));
            }
            char buffer[4096];
            while (rest.size() < length && !stop_.load()) {
                pollfd descriptor{client, POLLIN, 0};
                if (::poll(&descriptor, 1, 20) <= 0) {
                    continue;
                }
                const auto count = ::recv(client, buffer, sizeof(buffer), 0);
                if (count <= 0) {
                    break;
                }
                rest.append(buffer, static_cast<size_t>(count));
            }
            request.body = rest;
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                requests_.push_back(request);
            }
            const FakeReply reply = handler_(request);
            const auto wait = [&] {
                for (int waited = 0; waited < reply.delayMs && !stop_.load(); waited += 10) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
            };
            if (reply.partialBytes < 0) {
                wait();
            }
            if (!reply.hangUp && !stop_.load()) {
                std::string out = "HTTP/1.1 " + std::to_string(reply.status) + " X\r\n";
                out += "Content-Length: " + std::to_string(reply.body.size()) + "\r\nConnection: close\r\n";
                out += "Content-Type: application/json\r\n";
                for (const auto& [name, value] : reply.headers) {
                    out += name + ": " + value + "\r\n";
                }
                out += "\r\n";
                out += reply.partialBytes >= 0 ? reply.body.substr(0, static_cast<size_t>(reply.partialBytes))
                                               : reply.body;
                ::send(client, out.data(), out.size(), 0);
                if (reply.partialBytes >= 0) {
                    wait(); // the rest of the body never comes
                }
            }
        }
        ::close(client);
    }

    Handler handler_;
    int listener_ = -1;
    int port_ = 0;
    std::atomic<bool> stop_{false};
    std::thread thread_;
    std::vector<std::thread> workers_;
    mutable std::mutex mutex_;
    std::vector<FakeRequest> requests_;
};

} // namespace mm::test
