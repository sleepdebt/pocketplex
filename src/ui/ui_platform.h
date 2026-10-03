/* ui/ui_platform.h: SDL-specific accessors for the UI layer.
 * Not included by CORE (testable) modules — only by APP code that uses SDL.
 */
#ifndef PP_UI_PLATFORM_H
#define PP_UI_PLATFORM_H

#include <SDL.h>
#include <SDL_ttf.h>

SDL_Renderer *plat_renderer(void);
SDL_Window   *plat_window(void);
TTF_Font     *ui_font(void);
TTF_Font     *ui_font_bold(void);

#endif /* PP_UI_PLATFORM_H */
