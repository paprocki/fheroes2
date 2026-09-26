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

#include "game_lan_lobby.h"

#include <cstdint>
#include <vector>

#include "audio.h"
#include "audio_manager.h"
#include "cursor.h"
#include "dialog.h"
#include "game_hotkeys.h"
#include "game_mainmenu_ui.h"
#include "icn.h"
#include "image.h"
#include "lan_session.h"
#include "localevent.h"
#include "mus.h"
#include "network.h"
#include "screen.h"
#include "settings.h"
#include "translations.h"
#include "ui_button.h"
#include "ui_dialog.h"
#include "ui_text.h"
#include "ui_window.h"

namespace
{
    // Parses a "JOIN <colorByte>" message. Returns PlayerColor::NONE if malformed.
    PlayerColor parseJoinMessage( const std::string & message )
    {
        if ( message.rfind( "JOIN ", 0 ) != 0 ) {
            return PlayerColor::NONE;
        }

        try {
            return static_cast<PlayerColor>( std::stoi( message.substr( 5 ) ) );
        }
        catch ( ... ) {
            return PlayerColor::NONE;
        }
    }

    // Builds a "ROSTER <color>:<ip>;<color>:<ip>;..." message covering every entry in
    // 'roster' except 'excludeColor' (so a peer never gets told about itself).
    std::string buildRosterMessage( const std::vector<std::pair<PlayerColor, std::string>> & roster, const PlayerColor excludeColor )
    {
        std::string message = "ROSTER";
        for ( const auto & entry : roster ) {
            if ( entry.first == excludeColor ) {
                continue;
            }

            message += ' ';
            message += std::to_string( static_cast<int>( entry.first ) );
            message += ':';
            message += entry.second;
            message += ';';
        }

        return message;
    }

    // Parses a "ROSTER <color>:<ip>;..." message into (color, ip) pairs.
    std::vector<std::pair<PlayerColor, std::string>> parseRosterMessage( const std::string & message )
    {
        std::vector<std::pair<PlayerColor, std::string>> roster;

        if ( message.rfind( "ROSTER", 0 ) != 0 ) {
            return roster;
        }

        size_t pos = 6; // length of "ROSTER"
        while ( pos < message.size() ) {
            if ( message[pos] == ' ' ) {
                ++pos;
                continue;
            }

            const size_t entryEnd = message.find( ';', pos );
            if ( entryEnd == std::string::npos ) {
                break;
            }

            const std::string entry = message.substr( pos, entryEnd - pos );
            pos = entryEnd + 1;

            const size_t colonPos = entry.find( ':' );
            if ( colonPos == std::string::npos ) {
                continue;
            }

            try {
                const PlayerColor color = static_cast<PlayerColor>( std::stoi( entry.substr( 0, colonPos ) ) );
                const std::string ip = entry.substr( colonPos + 1 );
                if ( !ip.empty() ) {
                    roster.emplace_back( color, ip );
                }
            }
            catch ( ... ) {
                // Skip a malformed entry rather than aborting the whole roster.
                continue;
            }
        }

        return roster;
    }
}

bool Game::LanLobbyHost( const std::vector<PlayerColor> & pendingColors )
{
    LAN::Session & session = LAN::Session::Get();

    Network::LanMessageListener listener;
    if ( !listener.start( session.getPort() ) ) {
        fheroes2::showStandardTextMessage( {}, _( "Unable to listen on the configured LAN port. It may already be in use." ), Dialog::OK );
        return false;
    }

    AudioManager::stopSounds();
    AudioManager::PlayMusicAsync( MUS::MAINMENU, Music::PlaybackMode::RESUME_AND_PLAY_INFINITE );

    const CursorRestorer cursorRestorer( true, Cursor::POINTER );

    fheroes2::drawMainMenuScreen();

    fheroes2::Display & display = fheroes2::Display::instance();

    const int32_t rowHeight = 20;
    const int32_t rowsHeight = static_cast<int32_t>( pendingColors.size() ) * rowHeight;

    fheroes2::StandardWindow background( 380, 140 + rowsHeight, true, display );
    const fheroes2::Rect & area = background.activeArea();

    fheroes2::Text header( _( "LAN Lobby" ), fheroes2::FontType::normalYellow() );
    header.draw( area.x + ( area.width - header.width() ) / 2, area.y + 10, display );

    fheroes2::Text body( _( "Share your IP address with the other players. Waiting for everyone to connect..." ), fheroes2::FontType::normalWhite() );
    body.draw( area.x + 10, area.y + 35, area.width - 20, display );

    const int32_t rowsY = area.y + 70;
    fheroes2::ImageRestorer rowsArea( display, area.x, rowsY, area.width, rowsHeight );

    std::vector<bool> connected( pendingColors.size(), false );
    std::vector<std::string> connectedIp( pendingColors.size() );

    const auto redrawStatusRows = [&]() {
        rowsArea.restore();

        int32_t y = rowsY;
        for ( size_t i = 0; i < pendingColors.size(); ++i ) {
            std::string line = Color::String( pendingColors[i] );
            line += ": ";
            line += connected[i] ? _( "connected" ) : _( "waiting..." );

            fheroes2::Text rowText( line, fheroes2::FontType::normalWhite() );
            rowText.draw( area.x + ( area.width - rowText.width() ) / 2, y, display );

            y += rowHeight;
        }

        display.render( { area.x, rowsY, area.width, rowsHeight } );
    };

    redrawStatusRows();

    const bool isEvilInterface = Settings::Get().isEvilInterfaceEnabled();

    fheroes2::Button buttonStart;
    background.renderButton( buttonStart, isEvilInterface ? ICN::BUTTON_SMALL_OKAY_EVIL : ICN::BUTTON_SMALL_OKAY_GOOD, 0, 1, { 20, 11 },
                              fheroes2::StandardWindow::Padding::BOTTOM_LEFT );

    fheroes2::Button buttonCancel;
    background.renderButton( buttonCancel, isEvilInterface ? ICN::BUTTON_SMALL_CANCEL_EVIL : ICN::BUTTON_SMALL_CANCEL_GOOD, 0, 1, { 20, 11 },
                              fheroes2::StandardWindow::Padding::BOTTOM_RIGHT );

    fheroes2::validateFadeInAndRender();

    LocalEvent & le = LocalEvent::Get();

    while ( le.HandleEvents() ) {
        buttonStart.drawOnState( le.isMouseLeftButtonPressedAndHeldInArea( buttonStart.area() ) );
        buttonCancel.drawOnState( le.isMouseLeftButtonPressedAndHeldInArea( buttonCancel.area() ) );

        if ( le.MouseClickLeft( buttonCancel.area() ) || Game::HotKeyPressEvent( Game::HotKeyEvent::DEFAULT_CANCEL ) ) {
            listener.stop();
            return false;
        }

        if ( le.MouseClickLeft( buttonStart.area() ) || Game::HotKeyPressEvent( Game::HotKeyEvent::DEFAULT_OKAY ) ) {
            bool allConnected = true;
            for ( const bool oneConnected : connected ) {
                allConnected = allConnected && oneConnected;
            }

            if ( !allConnected ) {
                fheroes2::showStandardTextMessage( {}, _( "Not everyone has connected yet." ), Dialog::OK );
            }
            else {
                // Every peer already typed in the host's own IP to connect here in the first
                // place - there's no need for the host to determine its own address, so its
                // own roster entry uses the "HOST" sentinel instead of a real IP; the peer
                // side substitutes back the address it already used to reach us.
                std::vector<std::pair<PlayerColor, std::string>> roster;
                roster.emplace_back( session.getLocalColor(), "HOST" );
                for ( size_t i = 0; i < pendingColors.size(); ++i ) {
                    roster.emplace_back( pendingColors[i], connectedIp[i] );
                }

                for ( size_t i = 0; i < pendingColors.size(); ++i ) {
                    const std::string rosterMessage = buildRosterMessage( roster, pendingColors[i] );
                    Network::sendMessage( connectedIp[i], session.getPort(), rosterMessage );
                }

                listener.stop();
                return true;
            }
        }

        std::string message;
        std::string senderIp;
        bool changed = false;

        while ( listener.pollMessage( message, senderIp ) ) {
            const PlayerColor joinedColor = parseJoinMessage( message );
            if ( joinedColor == PlayerColor::NONE ) {
                continue;
            }

            for ( size_t i = 0; i < pendingColors.size(); ++i ) {
                if ( pendingColors[i] == joinedColor ) {
                    connected[i] = true;
                    connectedIp[i] = senderIp;
                    session.setPeerIp( joinedColor, senderIp );
                    changed = true;
                    break;
                }
            }
        }

        if ( changed ) {
            redrawStatusRows();
            display.render( background.totalArea() );
        }

        if ( listener.hasError() ) {
            listener.stop();
            fheroes2::showStandardTextMessage( {}, _( "The LAN listener encountered an error." ), Dialog::OK );
            return false;
        }
    }

    listener.stop();
    return false;
}

bool Game::LanLobbyConnect( const std::string & hostIp, const uint16_t port, const PlayerColor localColor )
{
    LAN::Session & session = LAN::Session::Get();

    Network::LanMessageListener listener;
    if ( !listener.start( port ) ) {
        fheroes2::showStandardTextMessage( {}, _( "Unable to listen on the configured LAN port. It may already be in use." ), Dialog::OK );
        return false;
    }

    if ( !Network::sendMessage( hostIp, port, "JOIN " + std::to_string( static_cast<int>( localColor ) ) ) ) {
        listener.stop();
        fheroes2::showStandardTextMessage( {}, _( "Failed to reach the host. Check the IP address and that the host has started the lobby." ), Dialog::OK );
        return false;
    }

    AudioManager::stopSounds();
    AudioManager::PlayMusicAsync( MUS::MAINMENU, Music::PlaybackMode::RESUME_AND_PLAY_INFINITE );

    const CursorRestorer cursorRestorer( true, Cursor::POINTER );

    fheroes2::drawMainMenuScreen();

    fheroes2::Display & display = fheroes2::Display::instance();
    fheroes2::StandardWindow background( 380, 130, true, display );
    const fheroes2::Rect & area = background.activeArea();

    fheroes2::Text header( _( "LAN Lobby" ), fheroes2::FontType::normalYellow() );
    header.draw( area.x + ( area.width - header.width() ) / 2, area.y + 10, display );

    fheroes2::Text body( _( "Connected to host. Waiting for the game to start..." ), fheroes2::FontType::normalWhite() );
    body.draw( area.x + 10, area.y + 45, area.width - 20, display );

    const bool isEvilInterface = Settings::Get().isEvilInterfaceEnabled();

    fheroes2::Button buttonCancel;
    background.renderButton( buttonCancel, isEvilInterface ? ICN::BUTTON_SMALL_CANCEL_EVIL : ICN::BUTTON_SMALL_CANCEL_GOOD, 0, 1, { 0, 11 },
                              fheroes2::StandardWindow::Padding::BOTTOM_CENTER );

    fheroes2::validateFadeInAndRender();

    LocalEvent & le = LocalEvent::Get();

    while ( le.HandleEvents() ) {
        buttonCancel.drawOnState( le.isMouseLeftButtonPressedAndHeldInArea( buttonCancel.area() ) );

        if ( le.MouseClickLeft( buttonCancel.area() ) || Game::HotKeyPressEvent( Game::HotKeyEvent::DEFAULT_CANCEL ) ) {
            listener.stop();
            return false;
        }

        std::string message;
        std::string senderIp;

        while ( listener.pollMessage( message, senderIp ) ) {
            if ( message.rfind( "ROSTER", 0 ) != 0 ) {
                // Not a roster message - ignore anything unexpected rather than crash.
                continue;
            }

            const std::vector<std::pair<PlayerColor, std::string>> roster = parseRosterMessage( message );

            for ( const auto & entry : roster ) {
                if ( entry.first == localColor ) {
                    continue;
                }

                // The host's own entry uses the "HOST" sentinel (see LanLobbyHost) instead of
                // a real IP - substitute back the address we already used to reach it.
                session.setPeerIp( entry.first, entry.second == "HOST" ? hostIp : entry.second );
            }

            listener.stop();
            return true;
        }

        if ( listener.hasError() ) {
            listener.stop();
            fheroes2::showStandardTextMessage( {}, _( "The LAN listener encountered an error." ), Dialog::OK );
            return false;
        }
    }

    listener.stop();
    return false;
}
