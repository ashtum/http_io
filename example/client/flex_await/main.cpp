//
// Copyright (c) 2024 Mohammad Nejati
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/http_io
//

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/http_io.hpp>
#include <boost/http_proto.hpp>
#include <boost/url.hpp>

#include <cstdlib>
#include <iostream>

#if defined(BOOST_ASIO_HAS_CO_AWAIT)

namespace asio       = boost::asio;
namespace core       = boost::core;
namespace http_io    = boost::http_io;
namespace http_proto = boost::http_proto;
namespace ssl        = boost::asio::ssl;
namespace urls       = boost::urls;

// Performs an HTTP GET and prints the response
asio::awaitable<void>
request(
    ssl::context& ssl_ctx,
    http_proto::context& http_proto_ctx,
    core::string_view host,
    core::string_view service,
    core::string_view target)
{
    auto exec   = co_await asio::this_coro::executor;
    auto stream = ssl::stream<asio::ip::tcp::socket>{ exec, ssl_ctx };

    {
        auto resolver = asio::ip::tcp::resolver{ exec };
        auto results  = co_await resolver.async_resolve(host, service);

        co_await asio::async_connect(stream.lowest_layer(), results);
    }

    if(auto host_s = std::string{ host };
       !SSL_set_tlsext_host_name(stream.native_handle(), host_s.c_str()))
    {
        throw boost::system::system_error(
            static_cast<int>(::ERR_get_error()),
            asio::error::get_ssl_category());
    }

    co_await stream.async_handshake(ssl::stream_base::client);

    {
        auto request = http_proto::request{};
        request.set_method(http_proto::method::get);
        request.set_target(target);
        request.set_version(http_proto::version::http_1_1);
        request.set_keep_alive(false);
        request.set(http_proto::field::host, host);
        request.set(http_proto::field::user_agent, "Boost.Http.Io");

        auto serializer = http_proto::serializer{ http_proto_ctx };
        serializer.start(request);

        co_await http_io::async_write(stream, serializer);
    }

    {
        auto parser = http_proto::response_parser{ http_proto_ctx };
        parser.reset();
        parser.start();

        co_await http_io::async_read_header(stream, parser);
        co_await http_io::async_read(stream, parser);

        std::cout << parser.body() << std::endl;
    }

    auto [ec] = co_await stream.async_shutdown(asio::as_tuple);
    if(ec && ec != ssl::error::stream_truncated)
        throw boost::system::system_error(ec);
};

int
main(int argc, char* argv[])
{
    try
    {
        if(argc != 2)
        {
            std::cerr << "Usage: flex_await <url>\n"
                      << "Example:\n"
                      << "    flex_await https://www.example.com\n";
            return EXIT_FAILURE;
        }

        auto url = urls::parse_uri(argv[1]);
        if(url.has_error())
        {
            std::cerr << "Failed to parse URL\n"
                      << "Error: " << url.error().what() << std::endl;
            return EXIT_FAILURE;
        }
        auto host    = url->host();
        auto service = url->has_port() ? url->port() : url->scheme();
        auto target =
            !url->encoded_target().empty() ? url->encoded_target() : "/";

        auto ioc            = asio::io_context{};
        auto ssl_ctx        = ssl::context{ ssl::context::tlsv12_client };
        auto http_proto_ctx = http_proto::context{};

        ssl_ctx.set_verify_mode(ssl::verify_none);

        {
            http_proto::request_parser::config cfg;
            http_proto::install_parser_service(http_proto_ctx, cfg);
        }

        asio::co_spawn(
            ioc,
            request(ssl_ctx, http_proto_ctx, host, service, target),
            [](std::exception_ptr ep)
            {
                if(ep)
                    std::rethrow_exception(ep);
            });

        ioc.run();
    }
    catch(std::exception const& e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

#else

int
main(int, char*[])
{
    std::cerr << "Coroutine examples require C++20" << std::endl;
    return EXIT_FAILURE;
}

#endif
