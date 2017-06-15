#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <http.h>

#include <cassert>

#include <string>
#include <vector>

#define BOOST_COROUTINES_NO_DEPRECATION_WARNING
#define BOOST_COROUTINES_V2
#include <boost/asio/spawn.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/windows/object_handle.hpp>
#include <boost/asio/windows/overlapped_ptr.hpp>
#include <boost/asio/detail/win_iocp_io_service.hpp>

namespace windows
{
    inline boost::system::error_code make_system_error_code( ULONG ec )
    {
        return{ static_cast< int >( ec ), boost::system::system_category() };
    }

    inline void throw_system_error( boost::system::error_code ec, std::string const& msg )
    {
        throw boost::system::system_error{ ec, msg };
    }

    inline void throw_system_error( ULONG ec, std::string const& msg )
    {
        throw_system_error( make_system_error_code( ec ), msg );
    }

    namespace http_server_api
    {
        inline USHORT cast_length( size_t len )
        {
            assert( ( std::numeric_limits< USHORT >::max )( ) >= len );

            return static_cast< USHORT >( len );
        }

        inline HTTP_DATA_CHUNK make_data_chunk( PVOID ptr, ULONG len )
        {
            HTTP_DATA_CHUNK chunk = { HttpDataChunkFromMemory };

            chunk.FromMemory = { ptr, len };

            return chunk;
        }

        inline HTTP_DATA_CHUNK make_data_chunk( std::vector< char >& v )
        {
            return make_data_chunk( v.data(), v.size() );
        }

        inline HTTP_DATA_CHUNK make_data_chunk( LPSTR s )
        {
            return make_data_chunk( static_cast< PVOID >( s ), strlen( s ) );
        }

        inline HTTP_RESPONSE& add_known_header( HTTP_RESPONSE& response, HTTP_HEADER_ID name, LPCSTR value, USHORT len )
        {
            response.Headers.KnownHeaders[ name ] = { len, value };

            return response;
        }

        inline HTTP_RESPONSE& add_known_header( HTTP_RESPONSE& response, HTTP_HEADER_ID name, LPCSTR value )
        {
            add_known_header( response, name, value, cast_length( strlen( value ) ) );

            return response;
        }

        inline HTTP_RESPONSE make_response( USHORT status_code, LPCSTR reason, USHORT reason_len )
        {
            HTTP_RESPONSE response{};

            response.StatusCode = status_code;
            response.pReason = reason;
            response.ReasonLength = reason_len;

            return response;
        }

        inline HTTP_RESPONSE make_response( USHORT status_code, LPCSTR reason )
        {
            return make_response( status_code, reason, cast_length( strlen( reason ) ) );
        }

        inline decltype(auto) get_win_iocp_io_service( boost::asio::io_service& io_service )
        {
            return boost::asio::use_service< boost::asio::detail::win_iocp_io_service >( io_service );
        }

        template< typename http_api_version_tag >
        class http_server_api_io_service_base
            : public boost::asio::detail::service_base< http_server_api_io_service_base< http_api_version_tag > >
        {
        public:
            http_server_api_io_service_base( boost::asio::io_service& io_service )
                : service_base{ io_service }
            {
                auto hr = HttpInitialize( http_api_version_tag::version(), HTTP_INITIALIZE_SERVER, nullptr );

                if( hr != NO_ERROR )
                {
                    throw_system_error( hr, "HttpInitialize" );
                }
            }

            void shutdown_service() override
            {
                auto hr = HttpTerminate( HTTP_INITIALIZE_SERVER, nullptr );
                assert( hr == NO_ERROR );
            }

        }; // http_server_api_io_service_base<>

        template< typename http_api_version_tag >
        struct http_handle_base : boost::asio::windows::object_handle
        {
            using boost::asio::windows::object_handle::object_handle;

        }; // http_handle_base<>

        namespace v1
        {
            struct Http_api_v1_tag
            {
                static HTTPAPI_VERSION const version()
                {
                    return HTTPAPI_VERSION_1;
                }

            }; // http_api_v1_tag

            using Http_handle = http_handle_base< Http_api_v1_tag >;

            // HTTP_REQUEST + std::vector< char >
            struct Request
            {
            public:
                explicit Request( size_t rq_size = 2048 )
                    : _buffer( sizeof( HTTP_REQUEST ) + rq_size )
                {
                }

                Request( Request&& ) = default;
                Request( Request const& ) = default;

                HTTP_REQUEST& raw_request()
                {
                    return *reinterpret_cast< HTTP_REQUEST* >( _buffer.data() );
                }

                HTTP_REQUEST const& raw_request() const
                {
                    return *reinterpret_cast< HTTP_REQUEST const* >( _buffer.data() );
                }

                size_t size() const
                {
                    return _buffer.size();
                }

                HTTP_REQUEST_ID id() const
                {
                    return raw_request().RequestId;
                }

                std::string url() const
                {
                    return raw_request().pRawUrl
                        ? raw_request().pRawUrl : "";
                }

            private:
                std::vector< char > _buffer;

            }; // Request

            class Http_io_service 
                : public http_server_api_io_service_base< Http_api_v1_tag >
            {
            public:
                using http_server_api_io_service_base::http_server_api_io_service_base;

                Http_handle create_http_request_queue()
                {
                    HANDLE queue{};

                    if( auto ec = HttpCreateHttpHandle( &queue, 0 ) )
                    {
                        throw_system_error( ec, "HttpCreateHttpHandle" );
                    }

                    auto& ios = get_io_service();

                    Http_handle hr{ ios, queue };

                    boost::system::error_code ec;
                    if( get_win_iocp_io_service( ios ).register_handle( queue, ec ) )
                    {
                        throw_system_error( ec, "win_iocp_io_service.register_handle" );
                    }

                    return hr;
                }

            }; // Http_io_service

            class Request_queue
            {
            public:
                explicit Request_queue( boost::asio::io_service& io_service )
                    : _handle{ boost::asio::use_service< Http_io_service >( io_service ).create_http_request_queue() }
                {
                }

                void add_url( wchar_t const* fully_qualified_url )
                {
                    auto hr = HttpAddUrl( _handle.native_handle(), fully_qualified_url, nullptr );

                    if( hr != NO_ERROR )
                    {
                        throw_system_error( hr, "HttpAddUrl" );
                    }
                }

                void add_url( std::wstring const& fully_qualified_url )
                {
                    add_url( fully_qualified_url.c_str() );
                }

                template< typename H >
                auto async_send_http_response( HTTP_RESPONSE& response, HTTP_REQUEST_ID request_id, H&& handler )
                {
                    boost::asio::detail::async_result_init< H, void( boost::system::error_code ) > init{
                        std::forward< H >( handler )
                    };

                    boost::asio::windows::overlapped_ptr op{ get_io_service(),
                        [ h = std::move( init.handler ) ]( auto error, auto ) mutable { h( error ); }
                    };

                    auto hr = HttpSendHttpResponse( _handle.native_handle(), request_id, 0,
                        &response, nullptr, nullptr, nullptr, 0, op.get(), nullptr );

                    if( hr != ERROR_IO_PENDING )
                    {
                        op.complete( make_system_error_code( hr ), 0 );
                    }
                    else
                    {
                        op.release();
                    }

                    return init.result.get();
                }

                template< typename H >
                auto async_receive_http_request( HTTP_REQUEST& request, ULONG request_length, HTTP_REQUEST_ID request_id, H&& handler )
                {
                    boost::asio::detail::async_result_init< H, void( boost::system::error_code ) > init{
                        std::forward< H >( handler )
                    };

                    boost::asio::windows::overlapped_ptr op{ get_io_service(),
                        [ h = std::move( init.handler ) ]( auto error, auto ) mutable { h( error ); }
                    };

                    auto hr = HttpReceiveHttpRequest( _handle.native_handle(), request_id, 0, &request, request_length, nullptr, op.get() );

                    if( hr != ERROR_IO_PENDING )
                    {
                        op.complete( make_system_error_code( hr ), 0 );
                    }
                    else
                    {
                        op.release();
                    }

                    return init.result.get();
                }

                boost::asio::io_service& get_io_service()
                {
                    return _handle.get_io_service();
                }

            private:
                Http_handle _handle;

            }; // Request_queue
        
        } // ns v1
    
    } // ns http_server_api

} // ns windows
