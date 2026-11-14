/*
 *  This file is part of Dune Legacy.
 *
 *  Dune Legacy is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Dune Legacy is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Dune Legacy.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef VERSION
    #define VERSION "1.0.868"
#endif

// Non-CMake builds also default to a development display label. Keep VERSION
// and VERSIONSTRING numeric for save/mod pins, multiplayer and update checks.
#ifndef DUNECITY_RELEASE_BUILD
    #define DUNECITY_RELEASE_BUILD 0
#endif
#if DUNECITY_RELEASE_BUILD
    #define DUNECITY_DISPLAY_VERSION VERSION
#else
    #define DUNECITY_DISPLAY_VERSION VERSION "dev"
#endif

#ifndef PACKAGE
    #define PACKAGE "dunecity"
#endif

#define VERSIONSTRING   PACKAGE VERSION

#ifndef DUNELEGACY_DATADIR
    #define DUNELEGACY_DATADIR "."
#endif

#ifndef CONFIGFILENAME
    #define CONFIGFILENAME "Dune City.ini"
#endif

#ifndef LOGFILENAME
    #define LOGFILENAME "Dune City.log"
#endif
