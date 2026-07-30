#pragma once
// Include from exactly ONE translation unit (the one defining BK_APP or main).
//
// Default mode (BK_APP): SDL provides the platform entry point and drives the
// SDL3 main-callbacks machinery; trampolines below forward into the library.
//
// #define BK_MAIN_HANDLED before including to write your own main() and call
// bk_run() yourself (native tools/tests only in v1).

#ifndef BK_MAIN_HANDLED
#define SDL_MAIN_USE_CALLBACKS 1
#endif
#include <SDL3/SDL_main.h>
#include <bielik/bk_app.h>

#define BK_APP(...)                                                                                \
    BK_AppDesc bk__app_desc(void) { return (BK_AppDesc){__VA_ARGS__}; }

// Forward declaration: BK_APP's expansion (the definition) appears later in
// the user's TU, textually after this header's #include line, so
// SDL_AppInit below needs this declared to pass it as a function pointer.
BK_AppDesc bk__app_desc(void);

#ifndef BK_MAIN_HANDLED
SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv) {
    return bk__boot(bk__app_desc, appstate, argc, argv);
}

SDL_AppResult SDL_AppIterate(void *appstate) { return bk__iterate(appstate); }

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) { return bk__event(appstate, event); }

void SDL_AppQuit(void *appstate, SDL_AppResult result) { bk__shutdown(appstate, result); }
#endif
