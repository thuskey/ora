// OpenRedAlert.cpp
// 1.0

//    This file is part of OpenRedAlert.
//
//    OpenRedAlert is free software: you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation, version 2 of the License.
//
//    OpenRedAlert is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with OpenRedAlert.  If not, see <http://www.gnu.org/licenses/>.

#include <iostream>
#include <exception>
#include <map>
#include <stdexcept>
#include <string>

#include <ctime>
#include <locale.h>

#include "SDL/SDL.h"

#include "audio/SoundEngine.h"
#include "game/Game.h"
#include "game/GameError.h"
#include "include/Logger.h"
#include "vfs/vfs.h"
#include "video/GraphicsEngine.h"
#include "video/VideoError.h"
#include "video/VQAMovie.h"
#include "video/WSAMovie.h"
#include "video/WSAError.h"

#define VERSION "439"

using std::abort;
using std::map;
using std::set_terminate;
using std::string;
using std::runtime_error;

using Sound::SoundEngine;

/** Logger for the application */
Logger *logger;

void PrintUsage(); // In args.cpp
bool parse(int argc, char** argv); // In args.cpp

// Below main
void cleanup();
void fcnc_terminate_handler();

namespace pc {
    extern ConfigType Config;
    extern vector<SHPImage *> *imagepool;
    extern GraphicsEngine * gfxeng;
    /** SoundEngine of the game */
    extern Sound::SoundEngine* sfxeng;
}

using VQA::VQAMovie;

int main(int argc, char** argv) {
    atexit(cleanup);
    set_terminate(fcnc_terminate_handler);

    // Correct the way that floats are readed
    setlocale(LC_ALL, "C");

    // Loads arguments
    if ((argc > 1) && ( string(argv[1]) == "-help" ||
            string(argv[1]) == "--help" || string(argv[1]) == "-?"))
    {
        PrintUsage();
        return EXIT_SUCCESS;
    }

    const string& binpath = determineBinaryLocation(argv[0]);
    string lf(binpath);
    lf += "/debug.log";

    VFSUtils::VFS_PreInit(binpath.c_str());
    // Log level is so that only errors are shown on stdout by default
    logger = new Logger(lf.c_str(), 0);
    if (!parse(argc, argv)) {
        return 1;
    }
    pc::Config = getConfig();

    pc::Config.binpath = binpath;
    //printf ("Binpath = %s\n",pc::Config.binpath.c_str());

    // Init with the path of the binaries
    VFSUtils::VFS_Init(binpath.c_str());
    VFSUtils::VFS_LoadGame(pc::Config.gamenum);
    // Log success of loading RA gmae
    logger->note(".MIX archives loading ok\n");

    // Load the start
    logger->note("Please wait, OpenRedAlert %s is starting\n", VERSION);

    try {
        if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_TIMER) < 0) {
            logger->error("Couldn't initialize SDL: %s\n", SDL_GetError());
            exit(1);
        }

        if (pc::Config.debug) {
            // Don't hide if we're debugging as the lag when running inside
            // valgrind is really irritating.
            logger->debug("Debug mode is enabled\n");
        }
        else {
            // Hide the cursor since we have our own.
            SDL_ShowCursor(0);
        }

        // Initialise Video
        try {
            logger->note("Initialising the graphics engine...");
            pc::gfxeng = new GraphicsEngine();
            logger->note("done\n");
        }
        catch (VideoError& ex) {
            logger->note("failed.  %s \n", ex.what());
            throw runtime_error("Unable to initialise the graphics engine");
        }

        // Initialise Sound
        logger->note("Initialising the sound engine...");
        pc::sfxeng = new SoundEngine(pc::Config.nosound);
        logger->note("done\n");

        // "Standalone" VQA Player
        if (pc::Config.playvqa) {
            logger->note("Now playing %s\n", pc::Config.vqamovie.c_str());
            try {
                VQA::VQAMovie mov(pc::Config.vqamovie.c_str());
                mov.play();
            }
            catch (runtime_error&) {
                logger->note("%s line %i: Failed to play movie: %s\n", __FILE__, __LINE__, pc::Config.vqamovie.c_str());
            }
        }

        // Play the intro if requested
        if (pc::Config.intro) {
            logger->note("Now playing the Introduction movie");
            try {
                VQAMovie mov("logo");
                mov.play();
            }
            catch (runtime_error&) {
            }

            try {
                WSAMovie* choose = new WSAMovie("choose.wsa");
                choose->animate(pc::gfxeng);
            }
            catch (WSAError&) {
            }
        }

        // "Standalone" VQA Player
        if (pc::Config.gamenum == GAME_RA) {
            //logger->note("Now playing %s\n", pc::Config.vqamovie.c_str());
            try {
                VQAMovie mov("english");
                mov.play();
            }
            catch (runtime_error&) {
                // Oke, failed to read the redalert intro, try the demo intro
                logger->note("%s line %i: Failed to play movie: english.vqa --> trying redintro.vqa\n", __FILE__, __LINE__);
                try {
                    VQAMovie mov("redintro.vqa");
                    mov.play();
                }
                catch (runtime_error&) {
                    logger->note("%s line %i: Failed to play movie: redintro.vqa\n", __FILE__, __LINE__);
                }
            }
        }

        // Init the rand functions
        srand(static_cast<unsigned int>(time(0)));

        // Stop all the music
        pc::sfxeng->StopMusic();
        pc::sfxeng->StopLoopedSound(-1);

        // Clean the graphics engine
        if (pc::gfxeng != 0) {
            delete pc::gfxeng;
        }
        pc::gfxeng = 0;

        try {
            // Initialise game engine
            logger->note("Initialising game engine:\n");
            Game gsession;
            // Start the game engine
            logger->note("Starting game\n");
            // Play the session
            gsession.play();
            // Log end off session
            logger->note("Shutting down\n");
        }
        catch (GameError&) {
            // Log it
            logger->error("Error during game\n");
        }
       
    }
    catch (runtime_error& e) {
        logger->error("%s\n", e.what());
        //#if _WIN32
        //MessageBox(0, e.what(), "Fatal error", MB_ICONERROR|MB_OK);
        //#endif
    }
    return 0;
}

/**
 * Wraps around a more verbose terminate handler and cleans up better
 */
void fcnc_terminate_handler() {
    cleanup();

#if __GNUC__ == 3 && __GNUC_MINOR__ >= 1 && ! defined (__MORPHOS__)
    // GCC 3.1+ feature, and is turned on by default for 3.4.
    using __gnu_cxx::__verbose_terminate_handler;
    __verbose_terminate_handler();
#else
    abort();
#endif
}

/**
 *
 */
void cleanup() {
    if (logger != NULL) {
        delete logger;
    }
    logger = NULL;

    VFSUtils::VFS_Destroy();
    SDL_Quit();
}
