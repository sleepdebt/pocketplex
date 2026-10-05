/* ui/ui_platform.h: SDL-specific accessors for the UI layer.
 * Not included by CORE (testable) modules — only by APP code that uses SDL.
 */
#ifndef PP_UI_PLATFORM_H
#define PP_UI_PLATFORM_H

#include <SDL.h>
#include <SDL_ttf.h>

SDL_Renderer *plat_renderer(void);
SDL_Window   *plat_window(void);

/* Internal (not platform.h): hand the display to an external player process.
 * suspend destroys the renderer + window and quits SDL video; resume brings
 * them back (0 ok, -1 failed; safe to retry). */
void plat_video_suspend(void);
int  plat_video_resume(void);

/* Access the bold font for screens that need it. Falls back to regular
 * if bold is unavailable. Implemented in ui.c. */
TTF_Font *ui_font(void);
TTF_Font *ui_font_bold(void);

#endif /* PP_UI_PLATFORM_H */
