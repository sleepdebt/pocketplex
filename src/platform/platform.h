/* platform/platform.h: device input/display contract.
 * Implemented by core (sdl2.c: desktop + SP) and core (miyoo.c, only if SDL2 is unusable on Onion).
 * Change requests go to the spec.
 */
#ifndef PP_PLATFORM_H
#define PP_PLATFORM_H

typedef enum { BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_A, BTN_B, BTN_X, BTN_Y,
               BTN_L1, BTN_R1, BTN_L2, BTN_R2, BTN_START, BTN_SELECT, BTN_MENU, BTN_NONE } pp_btn;

/* Opens the display; writes its size (640x480 on both handhelds). 0 = ok, <0 = error. */
int  plat_init(int *w, int *h);

/* Non-blocking; returns BTN_NONE when nothing is pressed. Key repeat is handled here.
 * A window-close / quit request is reported as BTN_MENU. */
pp_btn plat_poll_button(void);

void plat_present(void);
void plat_suspend_hook(void (*on_resume)(void)); /* Wi-Fi reconnect after sleep */
void plat_quit(void);

#endif /* PP_PLATFORM_H */
