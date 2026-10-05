/* ui/ui_stack.h: the screen stack. Pure, no SDL, unit-tested.
 * Screens leave the stack immediately on pop/replace/reset; they are destroyed
 * at end of frame (ui_stack_finalize), so a handler may still read its own
 * data after replacing itself. Every transition is logged (name + depth).
 */
#ifndef PP_UI_STACK_H
#define PP_UI_STACK_H

#include "ui/ui.h"

#define UI_STACK_MAX 16

/* 0 ok; -1 when full (the screen is destroyed, not leaked) or s is NULL. */
int  ui_stack_push(pp_screen *s);
/* Pop the top screen. Refused (-1) on the last screen: emptying the stack
 * quits the app, which only BTN_MENU / window close may do. */
int  ui_stack_pop(void);
/* Replace the top screen with s (pop + push as one step, works at the root). */
int  ui_stack_replace(pp_screen *s);
/* Drop every screen and make s the only one (sign-out, relink). */
int  ui_stack_reset(pp_screen *s);
/* Shutdown: drop every screen, root included. */
void ui_stack_pop_all(void);
/* Destroy screens removed this frame. Call once at end of frame. */
void ui_stack_finalize(void);

pp_screen *ui_stack_top(void);
int  ui_stack_depth(void);
const char *ui_screen_name(pp_screen_id id);

#endif /* PP_UI_STACK_H */
