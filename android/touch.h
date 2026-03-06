/*
 * Touch overlay controls for Android
 */

#ifndef _TOUCH_H
#define _TOUCH_H

#include <SDL.h>

// Initialize the touch overlay system. Call after GL context is ready.
void touch_overlay_init(void);

// Process an SDL event. Returns 1 if the event was consumed by the touch overlay.
int touch_overlay_process_event(SDL_Event *event);

// Draw the touch overlay. Call before swap buffers.
void touch_overlay_draw(int screen_w, int screen_h);

// Shut down the touch overlay.
void touch_overlay_close(void);

// Set whether the touch overlay is in menu mode (mouse clicks) or game mode (sticks+buttons)
void touch_overlay_set_game_mode(int in_game);

#endif // _TOUCH_H
