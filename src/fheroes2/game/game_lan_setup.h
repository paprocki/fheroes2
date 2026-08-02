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

#include "game_mode.h"

namespace Game
{
    // Offers to turn the Hot Seat game currently being set up into a LAN Hot Seat game:
    // pick which human color is played on this PC and enter every other human color's
    // peer IP address, storing the result in LAN::Session. Called after the player has
    // finished assigning colors/human-AI control for the scenario but before the map is
    // actually loaded.
    //
    // Returns true if it is fine to proceed to loading the map - either because LAN mode
    // was configured successfully, or because the user explicitly chose to play a plain
    // local Hot Seat game instead. Returns false only if the user cancelled out of LAN
    // setup partway through, in which case the caller should abandon starting the game
    // entirely (consistent with cancelling any other step of New Game setup).
    bool LanSetupHotSeat();

    // Configures this PC as a non-host participant in a LAN Hot Seat game: asks which color is played
    // locally and which port to listen on, then transitions to the "Waiting for LAN Turn" screen.
    // Returns fheroes2::GameMode::MAIN_MENU if the user cancels.
    fheroes2::GameMode JoinLanGame();
}
