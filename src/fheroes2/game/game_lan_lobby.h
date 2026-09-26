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

#include <cstdint>
#include <string>
#include <vector>

#include "color.h"

namespace Game
{
    // Host side of the LAN lobby: listens for a JOIN message from every color in
    // 'pendingColors', showing live connect status, until the host clicks Start (only
    // takes effect once everyone has joined) or Cancel. Peer IPs are learned from the
    // incoming connections themselves, not typed in - LAN::Session::Get()'s peer map is
    // populated as each one joins. On success, also broadcasts the full roster to every
    // connected peer so they know how to reach one another directly afterward. Returns
    // false if the host cancels.
    bool LanLobbyHost( const std::vector<PlayerColor> & pendingColors );

    // Peer side of the LAN lobby: connects to hostIp:port, sends a JOIN for localColor,
    // then waits for the host's roster reply, populating LAN::Session::Get()'s peer map
    // from it. Returns false on cancel or if the host can't be reached.
    bool LanLobbyConnect( const std::string & hostIp, uint16_t port, PlayerColor localColor );
}
