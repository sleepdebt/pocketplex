/* ui/ui_platform.h: SDL-specific accessors for the UI layer.
 * Not included by CORE (testable) modules — only by APP code that uses SDL.
 */
#ifndef PP_UI_PLATFORM_H
#define PP_UI_PLATFORM_H

#include <SDL.h>
#include <SDL_ttf.h>

SDL_Renderer *plat_renderer(void);
SDL_Window   *plat_window(void);

/* Access the bold font for screens that need it. Falls back to regular
 * if bold is unavailable. Implemented in ui.c. */
TTF_Font *ui_font(void);
TTF_Font *ui_font_bold(void);

#endif /* PP_UI_PLATFORM_H */
