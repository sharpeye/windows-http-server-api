#include "stdafx.h"
#include <http.h>
#include <iostream>
#include <thread>
#include "http-server-api.h"

#pragma comment(lib, "httpapi.lib")

/*
static boost::asio::windows::object_handle create_request_queue( boost::asio::io_service& io_service, std::wstring const& name = {} )
{
    HANDLE queue{};
    auto hr = HttpCreateRequestQueue( HTTPAPI_VERSION_2, 
        name.empty() ? nullptr : name.c_str(), 
        nullptr, HTTP_CREATE_REQUEST_QUEUE_FLAG_CONTROLLER, &queue );

    if( hr != NO_ERROR )
    {
        throw_system_error( hr, "HttpCreateHttpHandle" );
    }

    return{ io_service, queue };
}
*/

using namespace windows::http_server_api;

static void handle_request( v1::Request_queue& queue, v1::Request& request, boost::asio::yield_context yield )
{
    try
    {
        std::clog << "[TRACE] handle_request: #" << request.id() << " | " << request.url() << '\n';

        HTTP_RESPONSE response = make_response( 200, "OK" );

        add_known_header( response, HttpHeaderContentType, "text/html" );

        HTTP_DATA_CHUNK content[] = { 
            make_data_chunk( "<H1>" ),
            make_data_chunk( "Hello!" ),
            make_data_chunk( "</H1>" )
        };

        response.EntityChunkCount = _countof( content );
        response.pEntityChunks = content;

        queue.async_send_http_response( response, request.id(), yield );
    }
    catch( std::exception& e )
    {
        std::cerr << "[ERROR] " << e.what() << '\n';
    }
}

static void do_http_server( v1::Request_queue& queue, boost::asio::yield_context yield )
{
    try
    {
        for( ;; )
        {
            v1::Request request;

            boost::system::error_code ec;
            queue.async_receive_http_request(
                request.raw_request(),
                request.size(), HTTP_NULL_ID, yield[ ec ] );

            if( ec )
            {
                std::cerr << "[WARN] do_http_server: " << ec << '\n';

                continue;
            }

            boost::asio::spawn( queue.get_io_service(), [ &queue, request = std::move( request ) ]( auto yield ) mutable
            {
                handle_request( queue, request, yield );
            } );
        }
    }
    catch( boost::system::system_error& e )
    {
        std::cerr << "[ERROR] do_http_server: " << e.code() << " | " << e.what() << '\n';
    }
    catch( std::exception& e )
    {
        std::cerr << "[ERROR] do_http_server: " << e.what() << '\n';
    }
}

int main()
{
    try
    {
        boost::asio::io_service io_service;

        v1::Request_queue queue{ io_service };

        queue.add_url( L"http://127.0.0.1:8080/test/1/" );

        boost::asio::spawn( io_service, [&queue]( auto yield )
        {
            do_http_server( queue, yield );
        } );

        io_service.run();
    }
    catch( boost::system::system_error& e )
    {
        std::cerr << "[ERROR] " << e.code() << " | " << e.what() << '\n';
    }
    catch( std::exception& e )
    {
        std::cerr << "[ERROR] " << e.what() << '\n';
    }

    return 0;
}
