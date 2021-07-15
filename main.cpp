#include <iostream>
#include <iterator>
#include <string>
#include <algorithm>
#include <utility>

#include <boost/lambda/lambda.hpp>
#include <boost/asio.hpp>
#include <boost/algorithm/string.hpp>

#include <fmt/format.h>

namespace asio = boost::asio;

using namespace std::placeholders;

class Url {
public:
    Url() = delete;

    explicit Url(const std::string& url) {
        int protocolEndPos = 0;
        int hostStartPos = 0;
        int pathStartPos = url.size();

        for (int i = 0; i < url.size(); ++i) {
            switch (url[i]) {
                case ':':
                    protocolEndPos = i;
                    hostStartPos = i += 3;
                    break;
                case '/':
                    pathStartPos = i;
                    i = url.size();
                    break;
            }
        }

        if (protocolEndPos != 0) {
            protocol_ = url.substr(0, protocolEndPos);
        }

        host_ = url.substr(hostStartPos, pathStartPos - hostStartPos);

        if (pathStartPos != url.size()) {
            path_ = url.substr(pathStartPos);
        }
    }

    Url(std::string host, std::string path)
        : protocol_("http"), host_(std::move(host)), path_(std::move(path)) {}

    Url(std::string protocol, std::string host, std::string path)
        : protocol_(std::move(protocol)), host_(std::move(host)), path_(std::move(path)) {}

    std::string GetFullUrl() const {
        return fmt::format("{}://{}{}", protocol_, host_, path_);
    }
    std::string GetProtocol() const {
        return protocol_;
    }
    std::string GetHost() const {
        return host_;
    }
    std::string GetPath() const {
        return path_;
    }
private:
    std::string protocol_ = "http";
    std::string host_;
    std::string path_ = "/";
};

class HttpClient {
    std::string method_;
    std::string body_;

    const std::string host_;
    const std::string path_;

    asio::ip::tcp::resolver &resolver_;
    asio::ip::tcp::socket sock_;

    std::string request_;
    asio::streambuf response_;

public:
    HttpClient(asio::io_service &io_service, asio::ip::tcp::resolver &resolver,
               std::string host, std::string path, std::string body, std::string method)
            : host_(std::move(host)), path_(std::move(path)), resolver_(resolver), sock_(io_service),
              method_(std::move(method)), body_(std::move(body)) {
        requestFields_.insert({
            {"Host", host_},
            {"User-Agent", "mycurl/1.0"}
        });
    }

    void Start() {
        resolver_.async_resolve(
                asio::ip::tcp::resolver::query(host_, "http"),
                [this](const boost::system::error_code &ec,
                       const asio::ip::tcp::resolver::iterator &it) {
                    if (ec) {
                        fmt::print(stderr, "Error resolving {}: {}\n", host_, ec.message());
                        return;
                    }

                    fmt::print("{}: resolved to {}:{}\n", host_,
                               it->endpoint().address().to_string(), it->endpoint().port());
                    do_connect(it->endpoint());
                });
    }

private:
    std::map<std::string, std::string> requestFields_;

    void do_connect(const asio::ip::tcp::endpoint &dest) {
        sock_.async_connect(
                dest, [this](const boost::system::error_code &ec) {
                    if (ec) {
                        fmt::print(stderr, "Error connecting to {}: {}\n", host_, ec.message());
                        return;
                    }

                    fmt::print("{}: connected to {}:{}\n", host_,
                               sock_.remote_endpoint().address().to_string(),
                               sock_.remote_endpoint().port());

                    do_send_http();
                }
        );
    }

    void do_send_http() {
        if (method_ == "POST") {
            requestFields_.emplace("Content-Length", std::to_string(body_.length()));
        }

        request_ = fmt::format("{} {} HTTP/1.1\r\n", method_, path_);

        for (const auto& field : requestFields_) {
            request_ += fmt::format("{}: {}\r\n", field.first, field.second);
        }
        request_ += "\r\n";

        if (method_ == "POST") {
            request_ += fmt::format("{}\r\n\r\n", body_);
        }

        asio::async_write(
                sock_, asio::buffer(request_),
                [this](const boost::system::error_code &ec, std::size_t size) {
                    if (ec) {
                        fmt::print(stderr, "Error sending {}: {}: {}\n", method_, ec.category().name(), ec.value());
                        return;
                    }

                    fmt::print("{}: sent {} bytes\n", host_, size);

                    do_recv_http_header();
                }
        );
    }

    void do_recv_http_header() {
        asio::async_read_until(
                sock_, response_, "\r\n\r\n",
                [this](const boost::system::error_code &ec, std::size_t size) {
                    if (ec) {
                        fmt::print(stderr, "Error receiving header: {}: {}\n", ec.category().name(), ec.value());
                        return;
                    }

                    fmt::format("{}: received {}, streambuf {}\n", host_, size, response_.size());

                    std::string header(
                            asio::buffers_begin(response_.data()),
                            asio::buffers_begin(response_.data()) + size);
                    response_.consume(size);

                    fmt::print("{}: header length {}\n{}\n", host_, header.size(), header);

                    size_t pos = header.find("Content-Length: ");
                    if (pos != std::string::npos) {
                        size_t len = std::strtoul(
                                header.c_str() + pos + sizeof("Content-Length: ") - 1,
                                nullptr, 10);
                        do_receive_http_body(len - response_.size());
                        return;
                    }

                    pos = header.find("Transfer-Encoding: chunked");
                    if (pos != std::string::npos) {
                        do_receive_http_chunked_body();
                        return;
                    }

                    fmt::print(stderr, "Unknown body length");
                });
    }

    void do_receive_http_body(size_t len) {
        asio::async_read(
                sock_, response_, asio::transfer_exactly(len),
                std::bind(&HttpClient::handle_http_body, this, _1, _2));
    }

    void do_receive_http_chunked_body() {
        asio::async_read_until(
                sock_, response_, "\r\n\r\n",
                std::bind(&HttpClient::handle_http_body, this, _1, _2));
    }

    void handle_http_body(const boost::system::error_code &ec,
                          std::size_t size) {
        if (ec) {
            fmt::print(stderr, "Error receiving body: {}: {}\n", ec.category().name(), ec.value());
            return;
        }

        fmt::format("{}: received {}, streambuf {}\n", host_, size, response_.size());

        const auto &data = response_.data();
        std::string body(asio::buffers_begin(data), asio::buffers_end(data));
        response_.consume(size);

        fmt::print("{}: body length {}\n{}", host_, body.size(), body);
    }
};

void docs(std::string programName) {
    if (programName.empty()) {
        programName = "mycurl";
    }

    fmt::print("Usage: {} [options...] <url>\n"
                " -d <data>   HTTP POST data\n"
                " -m <method> HTTP method (default: GET)\n",
                programName);
}

int main(int argc, char *argv[]) {
    std::string method = "GET";
    std::string body;

    if (argc < 2) {
        docs(argv[0]);
        return 0;
    }

    int c;
    while ((c = getopt(argc, argv, "m:d:")) != -1) {
        switch (c) {
            case 'm':
                method = optarg;
                boost::to_upper(method);
                break;
            case 'd':
                body = optarg;
                break;
            default:
                docs(argv[0]);
                return 0;
        }
    }

    asio::io_service io_service;
    asio::ip::tcp::resolver resolver(io_service);
    std::vector <std::unique_ptr<HttpClient>> clients;

    std::string fullUrl = argv[argc - 1];
    Url url(fullUrl);

    std::string host = url.GetHost();
    std::string path = url.GetPath();

    fmt::print("{}: fetching {}\n", host, path);

    std::unique_ptr <HttpClient> client(
            new HttpClient(
                    io_service, resolver, host, path, body, method));
    client->Start();

    clients.push_back(std::move(client));

    io_service.run();

    return 0;
}