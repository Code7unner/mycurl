#include <iostream>
#include <iterator>
#include <algorithm>
#include <utility>

#include <boost/lambda/lambda.hpp>
#include <boost/asio.hpp>
#include <boost/algorithm/string.hpp>

#include <fmt/format.h>

namespace asio = boost::asio;

using namespace std::placeholders;

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
              method_(std::move(method)), body_(std::move(body)) {}

    void Start() {
        resolver_.async_resolve(
                asio::ip::tcp::resolver::query(host_, "http"),
                [this](const boost::system::error_code &ec,
                       const asio::ip::tcp::resolver::iterator &it) {
                    if (ec) {
                        fmt::print(stderr, "Error resolving {}: {}\n", host_, ec.message());
                        return;
                    }

                    fmt::print("{}: resolved to {}\n", host_, it->endpoint());
                    do_connect(it->endpoint());
                });
    }

private:
    void do_connect(const asio::ip::tcp::endpoint &dest) {
        sock_.async_connect(
                dest, [this](const boost::system::error_code &ec) {
                    if (ec) {
                        fmt::print(stderr, "Error connecting to {}: {}\n", host_, ec.message());
                        return;
                    }

                    fmt::print("{}: connected to {}\n", host_, sock_.remote_endpoint());

                    if (method_ == "GET") {
                        do_send_http_get();
                    } else {
                        do_send_http_post();
                    }
                }
        );
    }

    void do_send_http_post() {
        std::string contentLength = std::to_string(body_.length());
        std::string requestTemplate = "POST {} HTTP/1.1\r\n"
                                      "Host: {}\r\n"
                                      "Content-Length: {}\r\n\r\n"
                                      "{}\r\n\r\n";
        request_ = fmt::format(requestTemplate, path_, host_, contentLength, body_);
        asio::async_write(
                sock_, asio::buffer(request_),
                [this](const boost::system::error_code &ec, std::size_t size) {
                    if (ec) {
                        fmt::print(stderr, "Error sending POST: {}: {}\n", ec.category().name(), ec.value());
                        return;
                    }

                    fmt::print("{}: sent {} bytes\n", host_, size);

                    do_recv_http_header();
                }
        );
    }

    void do_send_http_get() {
        request_ = fmt::format("GET {} HTTP/1.1\r\n"
                               "Host: {}\r\n\r\n",
                               path_, host_);
        asio::async_write(
                sock_, asio::buffer(request_),
                [this](const boost::system::error_code &ec, std::size_t size) {
                    if (ec) {
                        fmt::print(stderr, "Error sending GET: {}: {}\n", ec.category().name(), ec.value());
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

    //TODO: use host and path from cmd args
    std::string host = "jsonplaceholder.typicode.com";
    std::string path = "/posts";

    std::cout << host << ": fetching "
              << path << std::endl;

    std::unique_ptr <HttpClient> client(
            new HttpClient(
                    io_service, resolver, host, path, body, method));
    client->Start();

    clients.push_back(std::move(client));

    io_service.run();

    return 0;
}