#include <iostream>
#include <iterator>
#include <algorithm>
#include <utility>

#include <boost/lambda/lambda.hpp>
#include <boost/asio.hpp>
#include <boost/algorithm/string.hpp>

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
                        std::cout << "Error resolving " << host_ << ": "
                                  << ec.message();
                        return;
                    }

                    std::cout << host_ << ": resolved to " << it->endpoint()
                              << std::endl;
                    do_connect(it->endpoint());
                });
    }

private:
    void do_connect(const asio::ip::tcp::endpoint &dest) {
        sock_.async_connect(
                dest, [this](const boost::system::error_code &ec) {
                    if (ec) {
                        std::cout << "Error connecting to " << host_ << ": "
                                  << ec.message();
                        return;
                    }

                    std::cout << host_ << ": connected to "
                              << sock_.remote_endpoint() << std::endl;

                    if (method_ == "GET") {
                        do_send_http_get();
                    } else {
                        do_send_http_post();
                    }
                }
        );
    }

    void do_send_http_post() {
        request_ = "POST " + path_ + " HTTP/1.1\r\nHost: " + host_ + "\r\n\r\n" +
                   body_ + "\r\n\r\n";
        asio::async_write(
                sock_, asio::buffer(request_),
                [this](const boost::system::error_code &ec, std::size_t size) {
                    if (ec) {
                        std::cout << "Error sending POST " << ec;
                        return;
                    }

                    std::cout << host_ << ": sent " << size << " bytes";
                    std::cout << std::endl;

                    do_recv_http_header();
                }
        );
    }

    void do_send_http_get() {
        request_ = "GET " + path_ + " HTTP/1.1\r\nHost: " + host_ + "\r\n\r\n";
        asio::async_write(
                sock_, asio::buffer(request_),
                [this](const boost::system::error_code &ec, std::size_t size) {
                    if (ec) {
                        std::cout << "Error sending GET " << ec;
                        return;
                    }

                    std::cout << host_ << ": sent " << size << " bytes";
                    std::cout << std::endl;

                    do_recv_http_header();
                }
        );
    }

    void do_recv_http_header() {
        asio::async_read_until(
                sock_, response_, "\r\n\r\n",
                [this](const boost::system::error_code &ec, std::size_t size) {
                    if (ec) {
                        std::cout << "Error receiving header " << ec;
                        return;
                    }

                    std::cout << host_ << ": received " << size << ", streambuf "
                              << response_.size() << std::endl;

                    std::string header(
                            asio::buffers_begin(response_.data()),
                            asio::buffers_begin(response_.data()) + size);
                    response_.consume(size);

                    std::cout << host_
                              << ": header length " << header.size() << std::endl
                              << header << std::endl;

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

                    std::cout << "Unknown body length";
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
            std::cout << "Error receiving body " << ec;
            return;
        }

        std::cout << host_ << ": received " << size << ", streambuf "
                  << response_.size() << std::endl;

        const auto &data = response_.data();
        std::string body(asio::buffers_begin(data), asio::buffers_end(data));
        response_.consume(size);

        std::cout << host_ << ": body length "
                  << body.size() << std::endl;

        std::cout << body;
    }
};

int main(int argc, char *argv[]) {
    std::string method = "GET";
    std::string body;

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
                std::cout << "Usage: curl [options...] <url>\n";
                std::cout << " -d <data>   HTTP POST data\n";
                std::cout << " -m <method> HTTP method (default: GET)\n";
        }
    }

//    std::string url = argv[argc - 1];

    asio::io_service io_service;
    asio::ip::tcp::resolver resolver(io_service);
    std::vector<std::unique_ptr<HttpClient>> clients;

    std::string host = "jsonplaceholder.typicode.com";
    std::string path = "/todos/1";

    std::cout << host << ": fetching "
              << path << std::endl;

    std::unique_ptr<HttpClient> client(
            new HttpClient(
                    io_service, resolver, host, path, body, method));
    client->Start();

    clients.push_back(std::move(client));

    io_service.run();

    return 0;
}