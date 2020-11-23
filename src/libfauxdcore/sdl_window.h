#include <SDL.h>

#ifdef USE_SDL2
EXPORT void fauxd_set_sdl_window (SDL_Window * w);
EXPORT SDL_Window * fauxd_get_sdl_window ();
#endif
