/***************************************************************************
 *   fheroes2: https://github.com/ihhub/fheroes2                           *
 *   Copyright (C) 2026                                                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#include "network.h"

#include <cassert>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <vector>

#if defined( _WIN32 )
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment( lib, "ws2_32.lib" )

namespace
{
    using SocketHandle = SOCKET;
    const SocketHandle invalidSocket = INVALID_SOCKET;
}
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

namespace
{
    using SocketHandle = int;
    const SocketHandle invalidSocket = -1;
}
#endif

namespace
{
    // Generous sanity bound on an incoming payload size, purely to reject a garbage
    // length prefix (corrupted stream, or something other than this app talking to
    // us) before attempting to allocate a buffer for it.
    constexpr uint32_t maxPayloadSize = 256u * 1024u * 1024u;

    // Connect timeout for the sending side, so a wrong or offline peer address fails
    // quickly instead of hanging the caller's UI thread.
    constexpr int connectTimeoutSeconds = 5;

    // Reference-counted Winsock lifecycle. No-op on non-Windows platforms. Every
    // entry point in this file that touches a socket constructs one of these on the
    // stack for the duration of the call (or, for LanListener, for the duration of
    // the background thread), so WSAStartup/WSACleanup calls are always balanced
    // regardless of how many send/listen operations run concurrently.
    class WinsockGuard
    {
    public:
        WinsockGuard()
        {
#if defined( _WIN32 )
            WSADATA wsaData;
            _valid = ( WSAStartup( MAKEWORD( 2, 2 ), &wsaData ) == 0 );
#endif
        }

        WinsockGuard( const WinsockGuard & ) = delete;

        ~WinsockGuard()
        {
#if defined( _WIN32 )
            if ( _valid ) {
                WSACleanup();
            }
#endif
        }

        WinsockGuard & operator=( const WinsockGuard & ) = delete;

        bool isValid() const
        {
#if defined( _WIN32 )
            return _valid;
#else
            return true;
#endif
        }

    private:
#if defined( _WIN32 )
        bool _valid{ false };
#endif
    };

    void closeSocket( SocketHandle socket )
    {
        if ( socket == invalidSocket ) {
            return;
        }

#if defined( _WIN32 )
        closesocket( socket );
#else
        close( socket );
#endif
    }

    bool setNonBlocking( SocketHandle socket, bool nonBlocking )
    {
#if defined( _WIN32 )
        u_long mode = nonBlocking ? 1 : 0;
        return ioctlsocket( socket, FIONBIO, &mode ) == 0;
#else
        const int flags = fcntl( socket, F_GETFL, 0 );
        if ( flags == -1 ) {
            return false;
        }

        const int newFlags = nonBlocking ? ( flags | O_NONBLOCK ) : ( flags & ~O_NONBLOCK );
        return fcntl( socket, F_SETFL, newFlags ) == 0;
#endif
    }

    // Sends 'length' bytes from 'data', looping over partial sends. Returns false on
    // any socket error.
    bool sendAll( SocketHandle socket, const char * data, size_t length )
    {
        size_t sent = 0;
        while ( sent < length ) {
#if defined( _WIN32 )
            const int chunk = send( socket, data + sent, static_cast<int>( length - sent ), 0 );
#else
            const ssize_t chunk = send( socket, data + sent, length - sent, 0 );
#endif
            if ( chunk <= 0 ) {
                return false;
            }

            sent += static_cast<size_t>( chunk );
        }

        return true;
    }

    // Receives exactly 'length' bytes into 'data', looping over partial reads.
    // Returns false if the connection is closed early or a socket error occurs.
    bool recvAll( SocketHandle socket, char * data, size_t length )
    {
        size_t received = 0;
        while ( received < length ) {
#if defined( _WIN32 )
            const int chunk = recv( socket, data + received, static_cast<int>( length - received ), 0 );
#else
            const ssize_t chunk = recv( socket, data + received, length - received, 0 );
#endif
            if ( chunk <= 0 ) {
                return false;
            }

            received += static_cast<size_t>( chunk );
        }

        return true;
    }

    // Blocking connect with a timeout: switches the socket to non-blocking mode for
    // the duration of the connection attempt, waits on it via select(), then restores
    // blocking mode on success.
    bool connectWithTimeout( SocketHandle socket, const sockaddr * address, socklen_t addressLength, int timeoutSeconds )
    {
        if ( !setNonBlocking( socket, true ) ) {
            return false;
        }

#if defined( _WIN32 )
        const int connectResult = connect( socket, address, addressLength );
        const bool inProgress = ( connectResult != 0 ) && ( WSAGetLastError() == WSAEWOULDBLOCK );
#else
        const int connectResult = connect( socket, address, addressLength );
        const bool inProgress = ( connectResult != 0 ) && ( errno == EINPROGRESS );
#endif

        bool connected = ( connectResult == 0 );

        if ( !connected && inProgress ) {
            fd_set writeSet;
            FD_ZERO( &writeSet );
            FD_SET( socket, &writeSet );

            timeval timeout{};
            timeout.tv_sec = timeoutSeconds;
            timeout.tv_usec = 0;

            const int selectResult = select( static_cast<int>( socket ) + 1, nullptr, &writeSet, nullptr, &timeout );
            if ( selectResult > 0 ) {
                int socketError = 0;
#if defined( _WIN32 )
                int errorLength = sizeof( socketError );
                getsockopt( socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>( &socketError ), &errorLength );
#else
                socklen_t errorLength = sizeof( socketError );
                getsockopt( socket, SOL_SOCKET, SO_ERROR, &socketError, &errorLength );
#endif
                connected = ( socketError == 0 );
            }
        }

        setNonBlocking( socket, false );

        return connected;
    }
}

namespace Network
{
    bool sendFile( const std::string & host, const uint16_t port, const std::string & filePath )
    {
        std::ifstream file( filePath, std::ios::binary | std::ios::ate );
        if ( !file.is_open() ) {
            return false;
        }

        const std::streamsize fileSize = file.tellg();
        if ( fileSize < 0 || static_cast<uint64_t>( fileSize ) > maxPayloadSize ) {
            return false;
        }

        file.seekg( 0, std::ios::beg );

        std::vector<char> buffer( static_cast<size_t>( fileSize ) );
        if ( !buffer.empty() && !file.read( buffer.data(), fileSize ) ) {
            return false;
        }

        file.close();

        const WinsockGuard winsockGuard;
        if ( !winsockGuard.isValid() ) {
            return false;
        }

        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        addrinfo * resolvedAddress = nullptr;
        if ( getaddrinfo( host.c_str(), std::to_string( port ).c_str(), &hints, &resolvedAddress ) != 0 || resolvedAddress == nullptr ) {
            return false;
        }

        const SocketHandle socketHandle = socket( resolvedAddress->ai_family, resolvedAddress->ai_socktype, resolvedAddress->ai_protocol );
        if ( socketHandle == invalidSocket ) {
            freeaddrinfo( resolvedAddress );
            return false;
        }

        const bool connected = connectWithTimeout( socketHandle, resolvedAddress->ai_addr, static_cast<socklen_t>( resolvedAddress->ai_addrlen ), connectTimeoutSeconds );

        freeaddrinfo( resolvedAddress );

        if ( !connected ) {
            closeSocket( socketHandle );
            return false;
        }

        const uint32_t payloadSize = static_cast<uint32_t>( buffer.size() );
        const uint32_t payloadSizeNetworkOrder = htonl( payloadSize );

        const bool sendOk
            = sendAll( socketHandle, reinterpret_cast<const char *>( &payloadSizeNetworkOrder ), sizeof( payloadSizeNetworkOrder ) )
              && ( buffer.empty() || sendAll( socketHandle, buffer.data(), buffer.size() ) );

        closeSocket( socketHandle );

        return sendOk;
    }

    LanListener::~LanListener()
    {
        stop();
    }

    bool LanListener::start( const uint16_t port, std::string outputFilePath )
    {
        stop();

        {
            const std::scoped_lock<std::mutex> lock( _resultMutex );
            _outputFilePath = std::move( outputFilePath );
            _fileReady = false;
        }

        _errorFlag = false;
        _stopRequested = false;

        _thread = std::make_unique<std::thread>( &LanListener::threadMain, this, port );

        return true;
    }

    void LanListener::stop()
    {
        if ( !_thread ) {
            return;
        }

        _stopRequested = true;

        {
            const std::scoped_lock<std::mutex> lock( _resultMutex );
            closeSocket( static_cast<SocketHandle>( _listenSocket ) );
            _listenSocket = -1;
        }

        _thread->join();
        _thread.reset();
    }

    bool LanListener::pollReceivedFile( std::string & outFilePath )
    {
        const std::scoped_lock<std::mutex> lock( _resultMutex );
        if ( !_fileReady ) {
            return false;
        }

        _fileReady = false;
        outFilePath = _outputFilePath;

        return true;
    }

    bool LanListener::hasError() const
    {
        return _errorFlag.load();
    }

    void LanListener::threadMain( const uint16_t port )
    {
        const WinsockGuard winsockGuard;
        if ( !winsockGuard.isValid() ) {
            _errorFlag = true;
            return;
        }

        const SocketHandle listenSocket = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
        if ( listenSocket == invalidSocket ) {
            _errorFlag = true;
            return;
        }

        {
            const int reuse = 1;
#if defined( _WIN32 )
            setsockopt( listenSocket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>( &reuse ), sizeof( reuse ) );
#else
            setsockopt( listenSocket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof( reuse ) );
#endif
        }

        sockaddr_in serverAddress{};
        serverAddress.sin_family = AF_INET;
        serverAddress.sin_addr.s_addr = INADDR_ANY;
        serverAddress.sin_port = htons( port );

        if ( bind( listenSocket, reinterpret_cast<const sockaddr *>( &serverAddress ), sizeof( serverAddress ) ) != 0
             || listen( listenSocket, 1 ) != 0 ) {
            closeSocket( listenSocket );
            _errorFlag = true;
            return;
        }

        {
            const std::scoped_lock<std::mutex> lock( _resultMutex );
            _listenSocket = static_cast<std::intptr_t>( listenSocket );
        }

        while ( !_stopRequested ) {
            const SocketHandle clientSocket = accept( listenSocket, nullptr, nullptr );
            if ( clientSocket == invalidSocket ) {
                // Either a real socket error, or stop() closed the listening socket to
                // unblock us - either way, there is nothing more this thread can do.
                break;
            }

            uint32_t payloadSizeNetworkOrder = 0;
            std::vector<char> buffer;
            bool receivedOk = recvAll( clientSocket, reinterpret_cast<char *>( &payloadSizeNetworkOrder ), sizeof( payloadSizeNetworkOrder ) );

            if ( receivedOk ) {
                const uint32_t payloadSize = ntohl( payloadSizeNetworkOrder );
                if ( payloadSize > maxPayloadSize ) {
                    receivedOk = false;
                }
                else {
                    buffer.resize( payloadSize );
                    receivedOk = buffer.empty() || recvAll( clientSocket, buffer.data(), buffer.size() );
                }
            }

            closeSocket( clientSocket );

            if ( !receivedOk ) {
                // A malformed or interrupted transfer from this one peer shouldn't take
                // down the listener - go back to accepting the next connection.
                continue;
            }

            std::string outputFilePathCopy;
            {
                const std::scoped_lock<std::mutex> lock( _resultMutex );
                outputFilePathCopy = _outputFilePath;
            }

            std::ofstream outputFile( outputFilePathCopy, std::ios::binary | std::ios::trunc );
            if ( !outputFile.is_open() ) {
                continue;
            }

            if ( !buffer.empty() ) {
                outputFile.write( buffer.data(), static_cast<std::streamsize>( buffer.size() ) );
            }

            outputFile.close();

            {
                const std::scoped_lock<std::mutex> lock( _resultMutex );
                _fileReady = true;
            }
        }

        {
            const std::scoped_lock<std::mutex> lock( _resultMutex );
            if ( _listenSocket != -1 ) {
                closeSocket( static_cast<SocketHandle>( _listenSocket ) );
                _listenSocket = -1;
            }
        }
    }
}
