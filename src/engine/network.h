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

#pragma once

// Minimal TCP file-transfer helpers used by the LAN hot-seat hand-off feature.
//
// This is intentionally unhardened: it is meant for a trusted, in-person LAN party,
// not the open internet. There is no authentication and no integrity checking beyond
// a sanity bound on the declared payload size.

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace Network
{
    // Blocking. Connects to host:port (with a short connect timeout so an offline or
    // mistyped peer address doesn't hang the caller indefinitely), sends the contents
    // of filePath prefixed with its size, then closes the connection.
    // Returns false on any socket error, connect timeout, or if filePath can't be read.
    bool sendFile( const std::string & host, uint16_t port, const std::string & filePath );

    // Same connect-with-timeout behavior as sendFile(), but sends a short text message
    // instead of a file's contents. Used for the LAN lobby handshake (JOIN/ROSTER
    // messages), not for turn hand-off.
    bool sendMessage( const std::string & host, uint16_t port, const std::string & message );

    // Owns a background thread that listens on a TCP port and, for each incoming
    // connection, receives a size-prefixed payload and writes it to a fixed output
    // path (overwriting any previous contents), then goes back to listening.
    class LanListener
    {
    public:
        LanListener() = default;
        LanListener( const LanListener & ) = delete;
        ~LanListener();

        LanListener & operator=( const LanListener & ) = delete;

        // Starts the background thread listening on 'port'. Every file received while
        // running is written to 'outputFilePath'. Returns false if the listening
        // socket could not be created/bound (for example, the port is already in use).
        bool start( uint16_t port, std::string outputFilePath );

        // Stops the background thread, if running. Safe to call even if start() was
        // never called or already failed/stopped.
        void stop();

        // Non-blocking. Returns true at most once per completed receive (the "ready"
        // state is consumed by this call) and fills outFilePath with the path passed
        // to start(). Meant to be polled once per iteration of the caller's own event
        // loop.
        bool pollReceivedFile( std::string & outFilePath );

        // True if the background thread hit an unrecoverable socket error and is no
        // longer listening.
        bool hasError() const;

    private:
        void threadMain( uint16_t port );

        std::unique_ptr<std::thread> _thread;
        std::atomic<bool> _stopRequested{ false };
        std::atomic<bool> _errorFlag{ false };

        std::mutex _resultMutex;
        bool _fileReady{ false };
        std::string _outputFilePath;

        // Platform-specific listening socket handle, stored as a plain int/SOCKET-sized
        // value so this header doesn't need to pull in platform socket headers. Guarded
        // by _resultMutex only for the brief moment stop() needs to close it from a
        // different thread than the one that created it.
        std::intptr_t _listenSocket{ -1 };
    };

    // Owns a background thread that listens on a TCP port and, for each incoming
    // connection, receives a short text message and queues it along with the sender's
    // IP address (read from the connection itself, not from the message body - this is
    // what lets a listener auto-detect a peer's real address). Unlike LanListener,
    // multiple received messages are queued rather than the latest one overwriting the
    // previous - this is what the LAN lobby handshake needs (many small JOIN messages
    // arriving over time from different peers), whereas LanListener's "latest file
    // wins" behavior is exactly right for turn hand-off.
    class LanMessageListener
    {
    public:
        LanMessageListener() = default;
        LanMessageListener( const LanMessageListener & ) = delete;
        ~LanMessageListener();

        LanMessageListener & operator=( const LanMessageListener & ) = delete;

        // Starts the background thread listening on 'port'. Returns false if the
        // listening socket could not be created/bound.
        bool start( uint16_t port );

        // Stops the background thread, if running. Safe to call even if start() was
        // never called or already failed/stopped.
        void stop();

        // Non-blocking. Returns true and fills outMessage/outSenderIp if a message was
        // queued (removing it from the queue). Call repeatedly in a loop to drain more
        // than one message per frame.
        bool pollMessage( std::string & outMessage, std::string & outSenderIp );

        // True if the background thread hit an unrecoverable socket error and is no
        // longer listening.
        bool hasError() const;

    private:
        void threadMain( uint16_t port );

        std::unique_ptr<std::thread> _thread;
        std::atomic<bool> _stopRequested{ false };
        std::atomic<bool> _errorFlag{ false };

        std::mutex _queueMutex;
        std::deque<std::pair<std::string, std::string>> _messageQueue; // (message, senderIp)

        // See LanListener::_listenSocket for why this is stored as a plain intptr_t.
        std::intptr_t _listenSocket{ -1 };
    };
}
