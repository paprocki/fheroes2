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

#include "lan_session.h"

namespace LAN
{
    Session & Session::Get()
    {
        static Session session;
        return session;
    }

    std::string Session::getPeerIp( const PlayerColor color ) const
    {
        const int index = Color::GetIndex( color );
        if ( index < 0 || index >= static_cast<int>( _peerIpByColorIndex.size() ) ) {
            return {};
        }

        return _peerIpByColorIndex[index];
    }

    void Session::setPeerIp( const PlayerColor color, std::string ip )
    {
        const int index = Color::GetIndex( color );
        if ( index < 0 || index >= static_cast<int>( _peerIpByColorIndex.size() ) ) {
            return;
        }

        _peerIpByColorIndex[index] = std::move( ip );
    }

    void Session::reset()
    {
        _enabled = false;
        _localColor = PlayerColor::NONE;
        _port = defaultPort;
        _peerIpByColorIndex.fill( {} );
    }
}
