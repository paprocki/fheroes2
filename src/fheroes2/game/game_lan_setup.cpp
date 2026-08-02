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

#include "game_lan_setup.h"

#include <cstdint>
#include <string>

#include "color.h"
#include "dialog.h"
#include "dialog_selectitems.h"
#include "game.h"
#include "lan_session.h"
#include "players.h"
#include "settings.h"
#include "translations.h"
#include "ui_dialog.h"
#include "ui_text.h"

bool Game::LanSetupHotSeat()
{
    Settings & conf = Settings::Get();
    LAN::Session & session = LAN::Session::Get();
    session.reset();

    const int answer = fheroes2::showStandardTextMessage(
        {},
        _( "Play this Hot Seat game over LAN? Each other human player will run their own copy of the game on their own PC, and the game will send turns between machines automatically as each player finishes their turn." ),
        Dialog::YES | Dialog::NO );

    if ( answer != Dialog::YES ) {
        // Plain local Hot Seat - today's behavior, unchanged.
        return true;
    }

    const PlayerColorsSet humanColors = Players::HumanColors();
    if ( Color::Count( humanColors ) < 2 ) {
        fheroes2::showStandardTextMessage( {}, _( "LAN Hot Seat needs at least two human-controlled colors." ), Dialog::OK );
        return true;
    }

    const PlayerColor localColor = Dialog::selectPlayerColor( PlayerColor::NONE, static_cast<uint8_t>( humanColors ) );
    if ( localColor == PlayerColor::NONE ) {
        // Cancelled.
        return false;
    }

    int32_t port = static_cast<int32_t>( LAN::defaultPort );
    if ( !Dialog::SelectCount( _( "LAN port to use:" ), 1024, 65535, port ) ) {
        return false;
    }

    session.setLocalColor( localColor );
    session.setPort( static_cast<uint16_t>( port ) );

    const PlayerColorsVector colors( humanColors );
    for ( const PlayerColor color : colors ) {
        if ( color == localColor ) {
            continue;
        }

        std::string ip;
        const std::string prompt = Color::String( color ) + std::string( " " ) + _( "player's IP address:" );

        if ( !Dialog::inputString( fheroes2::Text{}, fheroes2::Text{ prompt, fheroes2::FontType::normalWhite() }, ip, 15, false, {} ) || ip.empty() ) {
            // Cancelled.
            return false;
        }

        session.setPeerIp( color, std::move( ip ) );
    }

    session.setEnabled( true );
    conf.SetGameType( conf.GameType() | Game::TYPE_NETWORK );

    return true;
}

fheroes2::GameMode Game::JoinLanGame()
{
    Settings & conf = Settings::Get();
    LAN::Session & session = LAN::Session::Get();
    session.reset();

    conf.SetGameType( Game::TYPE_HOTSEAT | Game::TYPE_NETWORK );

    // The host's map isn't loaded yet on this PC, so which colors are actually human-controlled
    // isn't known here - offer every color and trust the player to pick the one matching what the
    // host configured for them.
    const PlayerColor localColor = Dialog::selectPlayerColor( PlayerColor::NONE, static_cast<uint8_t>( Color::allPlayerColors() ) );
    if ( localColor == PlayerColor::NONE ) {
        // Cancelled.
        return fheroes2::GameMode::MAIN_MENU;
    }

    int32_t port = static_cast<int32_t>( LAN::defaultPort );
    if ( !Dialog::SelectCount( _( "LAN port to listen on:" ), 1024, 65535, port ) ) {
        return fheroes2::GameMode::MAIN_MENU;
    }

    session.setLocalColor( localColor );
    session.setPort( static_cast<uint16_t>( port ) );

    // Sending the turn onward (e.g. back to the host, or on to a third human player) needs a peer
    // IP for every OTHER human color, exactly like the host configures in LanSetupHotSeat() - the
    // map isn't loaded here, so this asks how many other human players there are rather than being
    // able to enumerate the actual human colors.
    int32_t otherPlayerCount = 1;
    if ( !Dialog::SelectCount( _( "How many other human players are in this game?" ), 1, 5, otherPlayerCount ) ) {
        return fheroes2::GameMode::MAIN_MENU;
    }

    uint8_t excludedColors = static_cast<uint8_t>( localColor );
    for ( int32_t i = 0; i < otherPlayerCount; ++i ) {
        const uint8_t availableColors = static_cast<uint8_t>( Color::allPlayerColors() ) & ~excludedColors;
        const PlayerColor peerColor = Dialog::selectPlayerColor( PlayerColor::NONE, availableColors );
        if ( peerColor == PlayerColor::NONE ) {
            // Cancelled.
            return fheroes2::GameMode::MAIN_MENU;
        }

        excludedColors |= static_cast<uint8_t>( peerColor );

        std::string ip;
        const std::string prompt = Color::String( peerColor ) + std::string( " " ) + _( "player's IP address:" );

        if ( !Dialog::inputString( fheroes2::Text{}, fheroes2::Text{ prompt, fheroes2::FontType::normalWhite() }, ip, 15, false, {} ) || ip.empty() ) {
            // Cancelled.
            return fheroes2::GameMode::MAIN_MENU;
        }

        session.setPeerIp( peerColor, std::move( ip ) );
    }

    session.setEnabled( true );

    return fheroes2::GameMode::LAN_WAITING;
}
