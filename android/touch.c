/*
 * Touch overlay controls for Android.
 *
 * Provides dual virtual thumbsticks, a fire zone, and action buttons.
 * Left stick: forward/back + slide left/right (injects key events)
 * Right stick: pitch + yaw (injects mouse motion events)
 * Fire zone: primary fire (injects mouse button events)
 * Action buttons: map, menu, secondary fire, flare, bomb, afterburner
 */

#ifdef __ANDROID__

#include <math.h>
#include <string.h>

#include <SDL.h>
#include <GLES/gl.h>

#include "touch.h"

/* ------------------------------------------------------------------ */
/* Constants                                                          */
/* ------------------------------------------------------------------ */

#define MAX_FINGERS      10
#define STICK_RADIUS     0.10f   /* fraction of screen height */
#define STICK_DEAD_ZONE  0.015f  /* fraction of screen height */
#define MOUSE_SENSITIVITY 8.0f   /* right stick to mouse motion multiplier */

/* Button layout: small circular buttons */
#define BTN_RADIUS       0.04f   /* fraction of screen height */
#define BTN_MARGIN       0.02f   /* margin from edge, fraction */

/* Circle drawing resolution */
#define CIRCLE_SEGMENTS  24

/* Screen zones (left half = left stick, right half = right stick + fire) */
#define SCREEN_SPLIT     0.45f   /* x < SCREEN_SPLIT = left zone */

/* Fire zone: right side, upper portion to leave room for aiming below */
#define FIRE_ZONE_LEFT   0.70f
#define FIRE_ZONE_TOP    0.15f
#define FIRE_ZONE_BOT    0.65f

/* ------------------------------------------------------------------ */
/* Touch zones                                                        */
/* ------------------------------------------------------------------ */

enum {
	ZONE_NONE = 0,
	ZONE_LEFT_STICK,
	ZONE_RIGHT_STICK,
	ZONE_FIRE,
	ZONE_BTN_SECONDARY,
	ZONE_BTN_MAP,
	ZONE_BTN_MENU,
	ZONE_BTN_FLARE,
	ZONE_BTN_BOMB,
	ZONE_BTN_REAR,
	/* Menu-mode zones */
	ZONE_MENU_ENTER,
	ZONE_MENU_KEYBOARD,
	ZONE_MENU_UP,
	ZONE_MENU_DOWN,
	NUM_ZONES
};

/* ------------------------------------------------------------------ */
/* Button definitions                                                 */
/* ------------------------------------------------------------------ */

typedef struct {
	float cx, cy;        /* center position (fraction of screen, 0..1) */
	int zone;
	SDL_Keycode key;     /* SDL key to inject */
	const char *label;   /* short label for drawing */
} touch_button_t;

static touch_button_t touch_buttons[] = {
	/* Right side buttons - stacked vertically on far right */
	{ 0.95f, 0.30f, ZONE_BTN_SECONDARY, SDLK_SPACE,  "2nd" },
	{ 0.95f, 0.50f, ZONE_BTN_FLARE,     SDLK_f,      "FLR" },
	{ 0.95f, 0.70f, ZONE_BTN_BOMB,      SDLK_b,      "BMB" },

	/* Top buttons */
	{ 0.05f, 0.06f, ZONE_BTN_MAP,       SDLK_TAB,    "MAP" },
	{ 0.50f, 0.06f, ZONE_BTN_REAR,      SDLK_r,      "RVW" },
	{ 0.95f, 0.06f, ZONE_BTN_MENU,      SDLK_ESCAPE, "ESC" },

	/* Bottom-left: nav for in-game menus (difficulty select, etc.) */
	{ 0.05f, 0.75f, ZONE_MENU_UP,       SDLK_UP,     "UP"  },
	{ 0.05f, 0.90f, ZONE_MENU_DOWN,     SDLK_DOWN,   "DN"  },
	{ 0.15f, 0.83f, ZONE_MENU_ENTER,    SDLK_RETURN, "OK"  },
};
#define NUM_BUTTONS (sizeof(touch_buttons) / sizeof(touch_buttons[0]))

/* Menu-mode buttons */
#define MENU_BTN_RADIUS  0.06f  /* slightly larger for easy tapping */

static touch_button_t menu_buttons[] = {
	{ 0.92f, 0.90f, ZONE_MENU_ENTER,    SDLK_RETURN,   "OK"  },
	{ 0.92f, 0.72f, ZONE_MENU_KEYBOARD, 0,              "KB"  },
	{ 0.92f, 0.36f, ZONE_MENU_UP,       SDLK_UP,       "UP"  },
	{ 0.92f, 0.54f, ZONE_MENU_DOWN,     SDLK_DOWN,     "DN"  },
	{ 0.92f, 0.12f, ZONE_BTN_MENU,      SDLK_ESCAPE,   "ESC" },
};
#define NUM_MENU_BUTTONS (sizeof(menu_buttons) / sizeof(menu_buttons[0]))

/* ------------------------------------------------------------------ */
/* Finger tracking                                                    */
/* ------------------------------------------------------------------ */

typedef struct {
	SDL_FingerID id;
	float x, y;           /* current position (0..1, normalized) */
	float start_x, start_y;
	int zone;
	int active;
} touch_finger_t;

static touch_finger_t fingers[MAX_FINGERS];
static int in_game_mode = 0;

/* Left stick state */
static float lstick_cx, lstick_cy;     /* center of active left stick */
static float lstick_dx, lstick_dy;     /* current deflection (-1..1) */
static int   lstick_active = 0;

/* Right stick state */
static float rstick_cx, rstick_cy;
static float rstick_dx, rstick_dy;
static int   rstick_active = 0;

/* Fire state */
static int fire_active = 0;

/* Key state tracking to avoid repeat injections.
 * Left stick maps to game defaults: A=accelerate, Z=reverse, PAD1=slide left, PAD3=slide right */
static int key_fwd_down = 0, key_rev_down = 0;
static int key_sleft_down = 0, key_sright_down = 0;

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

static void push_key_event(SDL_Keycode sym, int down)
{
	SDL_Event ev;
	memset(&ev, 0, sizeof(ev));
	ev.type = down ? SDL_KEYDOWN : SDL_KEYUP;
	ev.key.state = down ? SDL_PRESSED : SDL_RELEASED;
	ev.key.keysym.sym = sym;
	ev.key.keysym.scancode = SDL_GetScancodeFromKey(sym);
	SDL_PushEvent(&ev);
}

static void push_mouse_button(int button, int down)
{
	SDL_Event ev;
	memset(&ev, 0, sizeof(ev));
	ev.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
	ev.button.button = button;
	ev.button.state = down ? SDL_PRESSED : SDL_RELEASED;
	ev.button.x = 0;
	ev.button.y = 0;
	SDL_PushEvent(&ev);
}

static void push_mouse_motion(int xrel, int yrel)
{
	if (xrel == 0 && yrel == 0)
		return;
	SDL_Event ev;
	memset(&ev, 0, sizeof(ev));
	ev.type = SDL_MOUSEMOTION;
	ev.motion.xrel = xrel;
	ev.motion.yrel = yrel;
	ev.motion.state = fire_active ? SDL_BUTTON_LMASK : 0;
	SDL_PushEvent(&ev);
}

static void push_mouse_click(float x, float y, int down, int screen_w, int screen_h)
{
	SDL_Event ev;
	memset(&ev, 0, sizeof(ev));
	ev.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
	ev.button.button = SDL_BUTTON_LEFT;
	ev.button.state = down ? SDL_PRESSED : SDL_RELEASED;
	ev.button.x = (int)(x * screen_w);
	ev.button.y = (int)(y * screen_h);
	SDL_PushEvent(&ev);

	/* Also push a motion event so the cursor is at the right place */
	if (down) {
		SDL_Event mev;
		memset(&mev, 0, sizeof(mev));
		mev.type = SDL_MOUSEMOTION;
		mev.motion.x = ev.button.x;
		mev.motion.y = ev.button.y;
		SDL_PushEvent(&mev);
	}
}

static touch_finger_t *find_finger(SDL_FingerID id)
{
	for (int i = 0; i < MAX_FINGERS; i++)
		if (fingers[i].active && fingers[i].id == id)
			return &fingers[i];
	return NULL;
}

static touch_finger_t *alloc_finger(SDL_FingerID id)
{
	for (int i = 0; i < MAX_FINGERS; i++)
		if (!fingers[i].active) {
			memset(&fingers[i], 0, sizeof(touch_finger_t));
			fingers[i].id = id;
			fingers[i].active = 1;
			return &fingers[i];
		}
	return NULL;
}

static int classify_zone(float x, float y)
{
	/* Check buttons first */
	for (int i = 0; i < (int)NUM_BUTTONS; i++) {
		float dx = x - touch_buttons[i].cx;
		float dy = y - touch_buttons[i].cy;
		/* Use a slightly larger hit area than visual radius */
		float hit_r = BTN_RADIUS * 1.5f;
		if (dx*dx + dy*dy < hit_r*hit_r)
			return touch_buttons[i].zone;
	}

	/* Fire zone: only inside the visible red box */
	if (x >= FIRE_ZONE_LEFT && x <= 0.92f && y >= FIRE_ZONE_TOP && y <= FIRE_ZONE_BOT)
		return ZONE_FIRE;

	/* Left half = left stick */
	if (x < SCREEN_SPLIT)
		return ZONE_LEFT_STICK;

	/* Right half = right stick (for aiming) */
	return ZONE_RIGHT_STICK;
}

/* ------------------------------------------------------------------ */
/* Stick update logic                                                 */
/* ------------------------------------------------------------------ */

static void update_left_stick(void)
{
	float threshold = STICK_DEAD_ZONE / STICK_RADIUS;
	int want_fwd = 0, want_rev = 0, want_sleft = 0, want_sright = 0;

	if (lstick_active) {
		if (lstick_dy < -threshold) want_fwd = 1;    /* up = accelerate (A key) */
		if (lstick_dy > threshold)  want_rev = 1;    /* down = reverse (Z key) */
		if (lstick_dx < -threshold) want_sleft = 1;  /* left = slide left (PAD1) */
		if (lstick_dx > threshold)  want_sright = 1; /* right = slide right (PAD3) */
	}

	if (want_fwd != key_fwd_down)       { push_key_event(SDLK_a,    want_fwd);    key_fwd_down = want_fwd; }
	if (want_rev != key_rev_down)       { push_key_event(SDLK_z,    want_rev);    key_rev_down = want_rev; }
	if (want_sleft != key_sleft_down)   { push_key_event(SDLK_KP_1, want_sleft);  key_sleft_down = want_sleft; }
	if (want_sright != key_sright_down) { push_key_event(SDLK_KP_3, want_sright); key_sright_down = want_sright; }
}

static void update_right_stick(int screen_w, int screen_h)
{
	if (!rstick_active)
		return;

	float threshold = STICK_DEAD_ZONE / STICK_RADIUS;
	if (fabsf(rstick_dx) < threshold && fabsf(rstick_dy) < threshold)
		return;

	int mx = (int)(rstick_dx * MOUSE_SENSITIVITY * (screen_w / 640.0f));
	int my = (int)(rstick_dy * MOUSE_SENSITIVITY * (screen_h / 480.0f));
	push_mouse_motion(mx, my);
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

void touch_overlay_init(void)
{
	memset(fingers, 0, sizeof(fingers));
	lstick_active = rstick_active = fire_active = 0;
	key_fwd_down = key_rev_down = key_sleft_down = key_sright_down = 0;
	in_game_mode = 0;
}

void touch_overlay_set_game_mode(int in_game)
{
	if (in_game_mode && !in_game) {
		/* Leaving game mode - release all keys */
		if (key_fwd_down)    { push_key_event(SDLK_a,    0); key_fwd_down = 0; }
		if (key_rev_down)    { push_key_event(SDLK_z,    0); key_rev_down = 0; }
		if (key_sleft_down)  { push_key_event(SDLK_KP_1, 0); key_sleft_down = 0; }
		if (key_sright_down) { push_key_event(SDLK_KP_3, 0); key_sright_down = 0; }
		if (fire_active) { push_mouse_button(SDL_BUTTON_LEFT, 0); fire_active = 0; }
		lstick_active = rstick_active = 0;
	}
	in_game_mode = in_game;
}

int touch_overlay_process_event(SDL_Event *event)
{
	if (event->type != SDL_FINGERDOWN &&
	    event->type != SDL_FINGERUP &&
	    event->type != SDL_FINGERMOTION)
		return 0;

	float x = event->tfinger.x;
	float y = event->tfinger.y;
	SDL_FingerID fid = event->tfinger.fingerId;

	if (!in_game_mode) {
		/* Menu mode: check menu buttons first, then pass as mouse click */
		int screen_w = 640, screen_h = 480;
		SDL_Window *win = SDL_GL_GetCurrentWindow();
		if (win)
			SDL_GetWindowSize(win, &screen_w, &screen_h);

		/* Check if touch hits a menu button */
		int menu_zone = ZONE_NONE;
		for (int i = 0; i < (int)NUM_MENU_BUTTONS; i++) {
			float dx = x - menu_buttons[i].cx;
			float dy = y - menu_buttons[i].cy;
			float hit_r = MENU_BTN_RADIUS * 1.5f;
			if (dx*dx + dy*dy < hit_r*hit_r) {
				menu_zone = menu_buttons[i].zone;
				break;
			}
		}

		if (menu_zone != ZONE_NONE) {
			if (menu_zone == ZONE_MENU_KEYBOARD) {
				if (event->type == SDL_FINGERDOWN) {
					if (SDL_IsTextInputActive())
						SDL_StopTextInput();
					else
						SDL_StartTextInput();
				}
			} else {
				/* Find the key for this zone */
				for (int i = 0; i < (int)NUM_MENU_BUTTONS; i++) {
					if (menu_buttons[i].zone == menu_zone && menu_buttons[i].key) {
						if (event->type == SDL_FINGERDOWN)
							push_key_event(menu_buttons[i].key, 1);
						else if (event->type == SDL_FINGERUP)
							push_key_event(menu_buttons[i].key, 0);
						break;
					}
				}
			}
			return 1;
		}

		/* Regular touch → mouse click for menus */
		if (event->type == SDL_FINGERDOWN) {
			push_mouse_click(x, y, 1, screen_w, screen_h);
		} else if (event->type == SDL_FINGERUP) {
			push_mouse_click(x, y, 0, screen_w, screen_h);
		} else if (event->type == SDL_FINGERMOTION) {
			SDL_Event mev;
			memset(&mev, 0, sizeof(mev));
			mev.type = SDL_MOUSEMOTION;
			mev.motion.x = (int)(x * screen_w);
			mev.motion.y = (int)(y * screen_h);
			mev.motion.state = SDL_BUTTON_LMASK;
			SDL_PushEvent(&mev);
		}
		return 1;
	}

	/* Game mode */
	if (event->type == SDL_FINGERDOWN) {
		touch_finger_t *f = alloc_finger(fid);
		if (!f) return 1;

		f->x = x;
		f->y = y;
		f->start_x = x;
		f->start_y = y;
		f->zone = classify_zone(x, y);

		switch (f->zone) {
		case ZONE_LEFT_STICK:
			lstick_cx = x;
			lstick_cy = y;
			lstick_dx = lstick_dy = 0;
			lstick_active = 1;
			break;
		case ZONE_RIGHT_STICK:
			rstick_cx = x;
			rstick_cy = y;
			rstick_dx = rstick_dy = 0;
			rstick_active = 1;
			break;
		case ZONE_FIRE:
			fire_active = 1;
			push_mouse_button(SDL_BUTTON_LEFT, 1);
			/* Also init right stick so dragging in fire zone aims */
			rstick_cx = x;
			rstick_cy = y;
			rstick_dx = rstick_dy = 0;
			rstick_active = 1;
			break;
		default:
			/* Action button - press key */
			for (int i = 0; i < (int)NUM_BUTTONS; i++) {
				if (touch_buttons[i].zone == f->zone) {
					push_key_event(touch_buttons[i].key, 1);
					break;
				}
			}
			break;
		}
		return 1;
	}

	if (event->type == SDL_FINGERUP) {
		touch_finger_t *f = find_finger(fid);
		if (!f) return 1;

		switch (f->zone) {
		case ZONE_LEFT_STICK:
			lstick_active = 0;
			lstick_dx = lstick_dy = 0;
			update_left_stick();
			break;
		case ZONE_RIGHT_STICK:
			rstick_active = 0;
			rstick_dx = rstick_dy = 0;
			break;
		case ZONE_FIRE:
			fire_active = 0;
			push_mouse_button(SDL_BUTTON_LEFT, 0);
			rstick_active = 0;
			rstick_dx = rstick_dy = 0;
			break;
		default:
			for (int i = 0; i < (int)NUM_BUTTONS; i++) {
				if (touch_buttons[i].zone == f->zone) {
					push_key_event(touch_buttons[i].key, 0);
					break;
				}
			}
			break;
		}

		f->active = 0;
		return 1;
	}

	if (event->type == SDL_FINGERMOTION) {
		touch_finger_t *f = find_finger(fid);
		if (!f) return 1;

		f->x = x;
		f->y = y;

		if (f->zone == ZONE_LEFT_STICK) {
			float dx = (x - lstick_cx) / STICK_RADIUS;
			float dy = (y - lstick_cy) / STICK_RADIUS;
			/* Clamp to unit circle */
			float len = sqrtf(dx*dx + dy*dy);
			if (len > 1.0f) { dx /= len; dy /= len; }
			lstick_dx = dx;
			lstick_dy = dy;
			update_left_stick();
		}
		else if (f->zone == ZONE_RIGHT_STICK || f->zone == ZONE_FIRE) {
			float dx = (x - rstick_cx) / STICK_RADIUS;
			float dy = (y - rstick_cy) / STICK_RADIUS;
			float len = sqrtf(dx*dx + dy*dy);
			if (len > 1.0f) { dx /= len; dy /= len; }
			rstick_dx = dx;
			rstick_dy = dy;
		}
		return 1;
	}

	return 0;
}

/* ------------------------------------------------------------------ */
/* Drawing helpers (GLES 1.x)                                         */
/* ------------------------------------------------------------------ */

static void draw_circle(float cx, float cy, float r, int segments, float alpha)
{
	GLfloat verts[(CIRCLE_SEGMENTS + 2) * 2];
	GLfloat colors[(CIRCLE_SEGMENTS + 2) * 4];

	/* Center vertex */
	verts[0] = cx;
	verts[1] = cy;
	colors[0] = 1.0f; colors[1] = 1.0f; colors[2] = 1.0f; colors[3] = alpha * 0.15f;

	for (int i = 0; i <= segments; i++) {
		float angle = (float)i / (float)segments * 2.0f * 3.14159265f;
		int vi = (i + 1) * 2;
		int ci = (i + 1) * 4;
		verts[vi]     = cx + cosf(angle) * r;
		verts[vi + 1] = cy + sinf(angle) * r;
		colors[ci]     = 1.0f;
		colors[ci + 1] = 1.0f;
		colors[ci + 2] = 1.0f;
		colors[ci + 3] = alpha * 0.3f;
	}

	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_COLOR_ARRAY);
	glVertexPointer(2, GL_FLOAT, 0, verts);
	glColorPointer(4, GL_FLOAT, 0, colors);
	glDrawArrays(GL_TRIANGLE_FAN, 0, segments + 2);
	glDisableClientState(GL_COLOR_ARRAY);
	glDisableClientState(GL_VERTEX_ARRAY);
}

static void draw_ring(float cx, float cy, float r, int segments, float r_col, float g_col, float b_col, float alpha)
{
	GLfloat verts[(CIRCLE_SEGMENTS + 1) * 2];
	GLfloat colors[(CIRCLE_SEGMENTS + 1) * 4];

	for (int i = 0; i <= segments; i++) {
		float angle = (float)i / (float)segments * 2.0f * 3.14159265f;
		int vi = i * 2;
		int ci = i * 4;
		verts[vi]     = cx + cosf(angle) * r;
		verts[vi + 1] = cy + sinf(angle) * r;
		colors[ci]     = r_col;
		colors[ci + 1] = g_col;
		colors[ci + 2] = b_col;
		colors[ci + 3] = alpha;
	}

	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_COLOR_ARRAY);
	glVertexPointer(2, GL_FLOAT, 0, verts);
	glColorPointer(4, GL_FLOAT, 0, colors);
	glLineWidth(2.0f);
	glDrawArrays(GL_LINE_LOOP, 0, segments + 1);
	glDisableClientState(GL_COLOR_ARRAY);
	glDisableClientState(GL_VERTEX_ARRAY);
}

static void draw_filled_circle(float cx, float cy, float r, int segments,
                               float r_col, float g_col, float b_col, float alpha)
{
	GLfloat verts[(CIRCLE_SEGMENTS + 2) * 2];
	GLfloat colors[(CIRCLE_SEGMENTS + 2) * 4];

	verts[0] = cx;
	verts[1] = cy;
	colors[0] = r_col; colors[1] = g_col; colors[2] = b_col; colors[3] = alpha;

	for (int i = 0; i <= segments; i++) {
		float angle = (float)i / (float)segments * 2.0f * 3.14159265f;
		int vi = (i + 1) * 2;
		int ci = (i + 1) * 4;
		verts[vi]     = cx + cosf(angle) * r;
		verts[vi + 1] = cy + sinf(angle) * r;
		colors[ci]     = r_col;
		colors[ci + 1] = g_col;
		colors[ci + 2] = b_col;
		colors[ci + 3] = alpha;
	}

	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_COLOR_ARRAY);
	glVertexPointer(2, GL_FLOAT, 0, verts);
	glColorPointer(4, GL_FLOAT, 0, colors);
	glDrawArrays(GL_TRIANGLE_FAN, 0, segments + 2);
	glDisableClientState(GL_COLOR_ARRAY);
	glDisableClientState(GL_VERTEX_ARRAY);
}

/* ------------------------------------------------------------------ */
/* Draw overlay                                                        */
/* ------------------------------------------------------------------ */

void touch_overlay_draw(int screen_w, int screen_h)
{
	/* Update right stick → mouse motion each frame (game mode only) */
	if (in_game_mode)
		update_right_stick(screen_w, screen_h);

	/* Save GL state */
	glPushMatrix();
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glOrthof(0.0f, (float)screen_w, (float)screen_h, 0.0f, -1.0f, 1.0f);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	glDisable(GL_TEXTURE_2D);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_DEPTH_TEST);

	float sh = (float)screen_h;
	float sw = (float)screen_w;
	float stick_r_px = STICK_RADIUS * sh;
	float btn_r_px = BTN_RADIUS * sh;

	/* Draw left stick (game mode only) */
	if (in_game_mode && lstick_active) {
		float cx = lstick_cx * sw;
		float cy = lstick_cy * sh;
		/* Outer ring */
		draw_ring(cx, cy, stick_r_px, CIRCLE_SEGMENTS, 1.0f, 1.0f, 1.0f, 0.3f);
		/* Inner nub showing deflection */
		float nx = cx + lstick_dx * stick_r_px;
		float ny = cy + lstick_dy * stick_r_px;
		draw_filled_circle(nx, ny, stick_r_px * 0.3f, CIRCLE_SEGMENTS, 1.0f, 1.0f, 1.0f, 0.5f);
	}

	/* Draw right stick (game mode only) */
	if (in_game_mode && rstick_active) {
		float cx = rstick_cx * sw;
		float cy = rstick_cy * sh;
		draw_ring(cx, cy, stick_r_px, CIRCLE_SEGMENTS, 0.5f, 0.8f, 1.0f, 0.3f);
		float nx = cx + rstick_dx * stick_r_px;
		float ny = cy + rstick_dy * stick_r_px;
		draw_filled_circle(nx, ny, stick_r_px * 0.3f, CIRCLE_SEGMENTS, 0.5f, 0.8f, 1.0f, 0.5f);
	}

	/* Draw fire zone indicator (game mode only) */
	if (in_game_mode) {
		float alpha = fire_active ? 0.4f : 0.15f;
		float fx = FIRE_ZONE_LEFT * sw;
		float fy1 = FIRE_ZONE_TOP * sh;
		float fy2 = FIRE_ZONE_BOT * sh;
		float fw = (0.92f - FIRE_ZONE_LEFT) * sw; /* up to just before the buttons */

		GLfloat verts[] = { fx, fy1, fx, fy2, fx+fw, fy2, fx+fw, fy1 };
		GLfloat cols[] = {
			1.0f, 0.3f, 0.3f, alpha,
			1.0f, 0.3f, 0.3f, alpha,
			1.0f, 0.3f, 0.3f, alpha,
			1.0f, 0.3f, 0.3f, alpha
		};

		glEnableClientState(GL_VERTEX_ARRAY);
		glEnableClientState(GL_COLOR_ARRAY);
		glVertexPointer(2, GL_FLOAT, 0, verts);
		glColorPointer(4, GL_FLOAT, 0, cols);
		glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
		glDisableClientState(GL_COLOR_ARRAY);
		glDisableClientState(GL_VERTEX_ARRAY);
	}

	if (in_game_mode) {
	/* Draw action buttons */
	for (int i = 0; i < (int)NUM_BUTTONS; i++) {
		float bx = touch_buttons[i].cx * sw;
		float by = touch_buttons[i].cy * sh;

		/* Check if this button is currently pressed */
		int pressed = 0;
		for (int j = 0; j < MAX_FINGERS; j++)
			if (fingers[j].active && fingers[j].zone == touch_buttons[i].zone)
				pressed = 1;

		float alpha = pressed ? 0.6f : 0.25f;
		draw_filled_circle(bx, by, btn_r_px, CIRCLE_SEGMENTS, 0.8f, 0.8f, 0.8f, alpha);
		draw_ring(bx, by, btn_r_px, CIRCLE_SEGMENTS, 1.0f, 1.0f, 1.0f, alpha + 0.1f);
	}
	} else {
	/* Menu mode: draw OK, KB, UP, DN buttons */
	float menu_r_px = MENU_BTN_RADIUS * sh;
	for (int i = 0; i < (int)NUM_MENU_BUTTONS; i++) {
		float bx = menu_buttons[i].cx * sw;
		float by = menu_buttons[i].cy * sh;
		float r_c = 0.3f, g_c = 0.8f, b_c = 0.3f;  /* green tint */
		if (menu_buttons[i].zone == ZONE_MENU_KEYBOARD)
			{ r_c = 0.3f; g_c = 0.5f; b_c = 1.0f; }  /* blue for keyboard */
		draw_filled_circle(bx, by, menu_r_px, CIRCLE_SEGMENTS, r_c, g_c, b_c, 0.35f);
		draw_ring(bx, by, menu_r_px, CIRCLE_SEGMENTS, r_c, g_c, b_c, 0.6f);
	}
	}

	/* Restore GL state */
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void touch_overlay_close(void)
{
	memset(fingers, 0, sizeof(fingers));
	lstick_active = rstick_active = fire_active = 0;
}

#endif /* __ANDROID__ */
