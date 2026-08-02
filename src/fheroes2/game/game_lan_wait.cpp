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

#include "game_lan_wait.h"

#include <string>

#include "audio.h"
#include "audio_manager.h"
#include "color.h"
#include "cursor.h"
#include "dialog.h"
#include "game.h"
#include "game_hotkeys.h"
#include "game_io.h"
#include "game_mainmenu_ui.h"
#include "icn.h"
#include "lan_session.h"
#include "localevent.h"
#include "mus.h"
#include "network.h"
#include "screen.h"
#include "settings.h"
#include "system.h"
#include "tools.h"
#include "translations.h"
#include "ui_button.h"
#include "ui_dialog.h"
#include "ui_text.h"
#include "ui_window.h"

fheroes2::GameMode Game::LanWaitForTurn()
{
    LAN::Session & session = LAN::Session::Get();

    const std::string incomingPath = System::concatPath( Game::GetSaveDir(), "LAN_INCOMING" + Game::GetSaveFileExtension() );

    Network::LanListener listener;
    if ( !listener.start( session.getPort(), incomingPath ) ) {
        fheroes2::showStandardTextMessage( {}, _( "Unable to listen on the configured LAN port. It may already be in use." ), Dialog::OK );
        session.reset();
        return fheroes2::GameMode::MAIN_MENU;
    }

    // Stop all sounds, but not the music
    AudioManager::stopSounds();
    AudioManager::PlayMusicAsync( MUS::MAINMENU, Music::PlaybackMode::RESUME_AND_PLAY_INFINITE );

    const CursorRestorer cursorRestorer( true, Cursor::POINTER );

    fheroes2::drawMainMenuScreen();

    fheroes2::Display & display = fheroes2::Display::instance();
    fheroes2::StandardWindow background( 380, 130, true, display );
    const fheroes2::Rect & area = background.activeArea();

    fheroes2::Text header( _( "Waiting for LAN Turn" ), fheroes2::FontType::normalYellow() );
    header.draw( area.x + ( area.width - header.width() ) / 2, area.y + 10, display );

    std::string bodyString = _( "Waiting to receive the turn for %{color} over the network..." );
    StringReplace( bodyString, "%{color}", Color::String( session.getLocalColor() ) );

    fheroes2::Text body( std::move( bodyString ), fheroes2::FontType::normalWhite() );
    body.draw( area.x + ( area.width - body.width() ) / 2, area.y + 45, display );

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
            session.reset();
            return fheroes2::GameMode::MAIN_MENU;
        }

        if ( le.isMouseRightButtonPressedInArea( buttonCancel.area() ) ) {
            fheroes2::showStandardTextMessage( _( "Cancel" ), _( "Stop waiting and return to the Main Menu." ), Dialog::ZERO );
        }

        std::string receivedPath;
        if ( listener.pollReceivedFile( receivedPath ) ) {
            listener.stop();

            Game::setPendingNetworkResumeMidRound( true );

            const fheroes2::GameMode loadResult = Game::Load( receivedPath );
            if ( loadResult == fheroes2::GameMode::CANCEL ) {
                // The received file failed to load (corrupted transfer, mismatched build, etc).
                // There is nothing more this PC can usefully do with it - go back to the Main Menu
                // rather than getting stuck.
                Game::consumePendingNetworkResumeMidRound();
                session.reset();
                return fheroes2::GameMode::MAIN_MENU;
            }

            return loadResult;
        }

        if ( listener.hasError() ) {
            listener.stop();
            fheroes2::showStandardTextMessage( {}, _( "The LAN listener encountered an error." ), Dialog::OK );
            session.reset();
            return fheroes2::GameMode::MAIN_MENU;
        }
    }

    listener.stop();
    session.reset();
    return fheroes2::GameMode::QUIT_GAME;
}
