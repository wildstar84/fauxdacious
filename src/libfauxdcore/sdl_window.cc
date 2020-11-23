#include "sdl_window.h"

#ifdef USE_SDL2
static SDL_Window * sdl_window;  /* JWT: MUST DECLARE VIDEO SCREEN-WINDOW HERE */

EXPORT void fauxd_set_sdl_window (SDL_Window * w)
{
    sdl_window = w;
}

EXPORT SDL_Window * fauxd_get_sdl_window ()
{
    return sdl_window;
}
#endif
