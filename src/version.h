#pragma once
/* release version: shown in the in-game overlay and used for the package name (tools/package.sh) */
#define ISAACLUA_VERSION "1.0.5"
#ifdef ISAACLUA_DEBUG  /* build.sh DEBUG=1: the autotest build (src/autotest.lua) */
#define ISAACLUA_VERSION_TEXT ISAACLUA_VERSION "-debug"
#else
#define ISAACLUA_VERSION_TEXT ISAACLUA_VERSION
#endif
