//
// Copyright (c) 2026 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/beast2
//

#include <boost/beast2/http_worker.hpp>
#include <boost/http/error.hpp>
#include <boost/http/io/any_buffer_sink.hpp>
#include <boost/http/io/any_buffer_source.hpp>
#include <boost/url/parse.hpp>
#include <iostream>

namespace boost {
namespace beast2 {

http_worker::
http_worker(
    http::router<http::route_params> fr_,
    http::parser::config const& parser_cfg,
    http::serializer::config const& serializer_cfg)
    : fr(std::move(fr_))
    , parser(parser_cfg)
    , serializer(serializer_cfg)
{
    // The reader and writer hold the address of the
    // stream member, not of the stream it wraps, so a
    // derived class may assign stream at any time
    // before the session starts.
    rp.req_body = http::any_buffer_source(
        http::message_reader(&stream, &parser));
    rp.res_body = http::any_buffer_sink(
        http::message_writer(&stream, &serializer));
}

capy::task<void>
http_worker::
do_http_session()
{
    struct guard
    {
        http_worker& self;

        guard(http_worker& self_)
            : self(self_)
        {
        }

        ~guard()
        {
            self.parser.reset();
            self.parser.start();
            self.rp.session_data.clear();
        }
    };

    guard g(*this); // clear things when session ends

    // read request, send response loop
    for(;;)
    {
        parser.reset();
        parser.start();
        rp.session_data.clear();

        // Read HTTP request header
        auto [ec] = co_await http::message_reader(
            &stream, &parser).read_header();
        if(ec)
        {
            std::cerr << "read_header error: " << ec.message() << "\n";
            break;
        }

        // Process headers and dispatch
        // Set up Request and Response objects
        rp.req = parser.get();
        rp.route_data.clear();
        rp.res.clear();
        rp.res.set_start_line(
            http::status::ok, rp.req.version());
        rp.res.set_keep_alive(rp.req.keep_alive());

        // NOTE: start() snapshots the framing (payload kind,
        // content length, content coding) from rp.res right
        // here, but handlers go on setting those fields until
        // their first write. Correct only while the whole body
        // fits the staging buffer; see serializer::start.
        serializer.start(&rp.res);

        // Parse the URL
        {
            auto rv = urls::parse_uri_reference(rp.req.target());
            if(rv.has_error())
            {
                rp.status(http::status::bad_request);
            }
            rp.url = rv.value();
        }

        {
            auto rv = co_await fr.dispatch(rp.req.method(), rp.url, rp);
            if(rv.failed())
            {
                // VFALCO log rv.error()
                break;
            }

            if(! rp.res.keep_alive())
                break;
        }
    }
}

} // beast2
} // boost
