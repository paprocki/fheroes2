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

#include <array>
#include <cstdint>
#include <string>

#include "color.h"

namespace LAN
{
    // Arbitrary port in the unregistered range.
    inline constexpr uint16_t defaultPort = 32410;

    // Per-machine LAN hot-seat configuration: which color this PC plays locally, and
    // which IP address to send the save file to for every other human-controlled
    // color. This is deliberately NOT part of Settings or the versioned save format -
    // it describes this physical PC's role in the LAN session, not game state, so it
    // has no business travelling inside a save file.
    class Session
    {
    public:
        static Session & Get();

        bool isEnabled() const
        {
            return _enabled;
        }

        void setEnabled( const bool enabled )
        {
            _enabled = enabled;
        }

        PlayerColor getLocalColor() const
        {
            return _localColor;
        }

        void setLocalColor( const PlayerColor color )
        {
            _localColor = color;
        }

        uint16_t getPort() const
        {
            return _port;
        }

        void setPort( const uint16_t port )
        {
            _port = port;
        }

        // Empty string means no peer address has been configured for this color.
        std::string getPeerIp( const PlayerColor color ) const;
        void setPeerIp( const PlayerColor color, std::string ip );

        // Clears local color, all peer addresses and disables LAN mode. Call when
        // cancelling LAN setup and when returning to the Main Menu.
        void reset();

    private:
        Session() = default;

        bool _enabled{ false };
        PlayerColor _localColor{ PlayerColor::NONE };
        uint16_t _port{ defaultPort };
        std::array<std::string, 6> _peerIpByColorIndex;
    };
}
