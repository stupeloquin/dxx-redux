/*
 * Touch overlay controls for Android.
 *
 * Provides a single consistent overlay for both menus and gameplay:
 * Left stick: thrust/slide in game, arrow keys in menus
 * Right stick: pitch + yaw (injects mouse motion events)
 * Fire zone: primary fire (injects mouse button events)
 * Action buttons: ESC, MAP, secondary fire, flare, bomb, rear view
 * Non-button taps become mouse clicks when a menu is active.
 */

#ifdef __ANDROID__

#include <math.h>
#include <string.h>

#include <SDL.h>
#include <GLES/gl.h>
#include <android/log.h>
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, "DXX-TOUCH", __VA_ARGS__)

#include "touch.h"
#include "playsave.h"

/* ------------------------------------------------------------------ */
/* Constants                                                          */
/* ------------------------------------------------------------------ */

#define MAX_FINGERS      10
#define STICK_RADIUS     0.10f   /* fraction of screen height */
#define STICK_DEAD_ZONE  0.025f  /* fraction of screen height */
#define MOUSE_SENSITIVITY 2.5f   /* right stick to mouse motion multiplier */
#define GYRO_SENSITIVITY  15.0f  /* gyroscope angular velocity to mouse motion */

/* Button layout: small circular buttons */
#define BTN_RADIUS       0.04f   /* fraction of screen height */

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
	ZONE_BTN_ROLL_LEFT,
	ZONE_BTN_ROLL_RIGHT,
	ZONE_BTN_HIDE,
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

	/* Roll buttons - bottom left area */
	{ 0.05f, 0.85f, ZONE_BTN_ROLL_LEFT,  SDLK_q,      "R_L" },
	{ 0.15f, 0.85f, ZONE_BTN_ROLL_RIGHT, SDLK_e,      "R_R" },

	/* Top buttons */
	{ 0.05f, 0.06f, ZONE_BTN_MAP,       SDLK_TAB,    "MAP" },
	{ 0.50f, 0.06f, ZONE_BTN_REAR,      SDLK_r,      "RVW" },
	{ 0.95f, 0.06f, ZONE_BTN_MENU,      SDLK_ESCAPE, "ESC" },
};
#define NUM_BUTTONS (sizeof(touch_buttons) / sizeof(touch_buttons[0]))

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
static int gameplay_active = 0;  /* 1 = pure gameplay, 0 = menu is on top */
static int touch_hidden = 0;

/* Hide/show button - always visible in bottom-right corner */
#define HIDE_BTN_CX     0.97f
#define HIDE_BTN_CY     0.97f
#define HIDE_BTN_RADIUS 0.025f  /* small, unobtrusive */

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

/* Key state tracking to avoid repeat injections */
static int key_up_down = 0, key_down_down = 0;
static int key_left_down = 0, key_right_down = 0;

/* Gyroscope sensor */
static SDL_Sensor *gyro_sensor = NULL;

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

	/* Fire zone: only active during gameplay */
	if (gameplay_active && x >= FIRE_ZONE_LEFT && x <= 0.92f &&
	    y >= FIRE_ZONE_TOP && y <= FIRE_ZONE_BOT)
		return ZONE_FIRE;

	/* Left half = left stick */
	if (x < SCREEN_SPLIT)
		return ZONE_LEFT_STICK;

	/* Right half: right stick during gameplay, none in menus
	 * (so taps fall through to mouse clicks) */
	if (gameplay_active)
		return ZONE_RIGHT_STICK;

	return ZONE_NONE;
}

/* ------------------------------------------------------------------ */
/* Stick update logic                                                 */
/* ------------------------------------------------------------------ */

static void release_left_stick_keys(void)
{
	if (key_up_down)    { push_key_event(gameplay_active ? SDLK_a      : SDLK_UP,    0); key_up_down = 0; }
	if (key_down_down)  { push_key_event(gameplay_active ? SDLK_z      : SDLK_DOWN,  0); key_down_down = 0; }
	if (key_left_down)  { push_key_event(gameplay_active ? SDLK_KP_1   : SDLK_LEFT,  0); key_left_down = 0; }
	if (key_right_down) { push_key_event(gameplay_active ? SDLK_KP_3   : SDLK_RIGHT, 0); key_right_down = 0; }
}

static void update_left_stick(void)
{
	float threshold = STICK_DEAD_ZONE / STICK_RADIUS;
	int want_up = 0, want_down = 0, want_left = 0, want_right = 0;

	if (lstick_active) {
		if (lstick_dy < -threshold) want_up = 1;
		if (lstick_dy > threshold)  want_down = 1;
		if (lstick_dx < -threshold) want_left = 1;
		if (lstick_dx > threshold)  want_right = 1;
	}

	/* In gameplay: A=thrust, Z=reverse, KP1=slide left, KP3=slide right
	 * In menus: arrow keys for navigation */
	SDL_Keycode key_u = gameplay_active ? SDLK_a    : SDLK_UP;
	SDL_Keycode key_d = gameplay_active ? SDLK_z    : SDLK_DOWN;
	SDL_Keycode key_l = gameplay_active ? SDLK_KP_1 : SDLK_LEFT;
	SDL_Keycode key_r = gameplay_active ? SDLK_KP_3 : SDLK_RIGHT;

	if (want_up != key_up_down)       { push_key_event(key_u, want_up);    key_up_down = want_up; }
	if (want_down != key_down_down)   { push_key_event(key_d, want_down);  key_down_down = want_down; }
	if (want_left != key_left_down)   { push_key_event(key_l, want_left);  key_left_down = want_left; }
	if (want_right != key_right_down) { push_key_event(key_r, want_right); key_right_down = want_right; }
}

static void update_right_stick(int screen_w, int screen_h)
{
	if (!rstick_active)
		return;

	float threshold = STICK_DEAD_ZONE / STICK_RADIUS;
	if (fabsf(rstick_dx) < threshold && fabsf(rstick_dy) < threshold)
		return;

	int mx = (int)(rstick_dx * MOUSE_SENSITIVITY * (screen_w / 640.0f));
	float pitch_dir = PlayerCfg.InvertTouchPitch ? -1.0f : 1.0f;
	int my = (int)(rstick_dy * pitch_dir * MOUSE_SENSITIVITY * (screen_h / 480.0f));
	push_mouse_motion(mx, my);
}

static int gyro_log_counter = 0;

static void update_gyroscope(int screen_w, int screen_h)
{
	float data[3];

	if (!gyro_sensor || !PlayerCfg.UseGyro)
		return;

	if (SDL_SensorGetData(gyro_sensor, data, 3) < 0) {
		if (gyro_log_counter++ % 300 == 0)
			LOGD("SDL_SensorGetData failed: %s", SDL_GetError());
		return;
	}

	/* Log raw data periodically for debugging */
	if (gyro_log_counter++ % 300 == 0)
		LOGD("gyro raw data: %.4f %.4f %.4f", data[0], data[1], data[2]);

	/* Gyro data is in the phone's natural (portrait) coordinate frame.
	 * In landscape mode the axes rotate 90 degrees:
	 * data[0]=X (portrait pitch) → landscape yaw (turn L/R)
	 * data[1]=Y (portrait yaw)   → landscape pitch (look U/D) */
	float yaw   = data[0];  /* portrait X axis = landscape turn left/right */
	float pitch = data[1];  /* portrait Y axis = landscape pitch up/down */

	/* Dead zone: ignore tiny movements (gyro drift) */
	float deadzone = 0.05f;
	if (fabsf(yaw) < deadzone) yaw = 0;
	if (fabsf(pitch) < deadzone) pitch = 0;

	if (yaw == 0 && pitch == 0)
		return;

	float pitch_dir = PlayerCfg.InvertTouchPitch ? -1.0f : 1.0f;
	int mx = (int)(yaw * GYRO_SENSITIVITY * (screen_w / 640.0f));
	int my = (int)(pitch * pitch_dir * GYRO_SENSITIVITY * (screen_h / 480.0f));
	push_mouse_motion(mx, my);
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

void touch_overlay_init(void)
{
	int i, n;

	LOGD("touch_overlay_init called");
	memset(fingers, 0, sizeof(fingers));
	lstick_active = rstick_active = fire_active = 0;
	key_up_down = key_down_down = key_left_down = key_right_down = 0;
	gameplay_active = 0;
	touch_hidden = 0;

	/* Open gyroscope sensor if available */
	gyro_sensor = NULL;
	if (SDL_InitSubSystem(SDL_INIT_SENSOR) < 0) {
		LOGD("SDL_InitSubSystem(SENSOR) failed: %s", SDL_GetError());
	} else {
		n = SDL_NumSensors();
		LOGD("SDL_NumSensors() = %d", n);
		for (i = 0; i < n; i++) {
			SDL_SensorType stype = SDL_SensorGetDeviceType(i);
			LOGD("sensor %d type=%d name=%s", i, (int)stype,
			        SDL_SensorGetDeviceName(i) ? SDL_SensorGetDeviceName(i) : "?");
			if (stype == SDL_SENSOR_GYRO) {
				gyro_sensor = SDL_SensorOpen(i);
				if (gyro_sensor)
					LOGD("gyroscope opened OK");
				else
					LOGD("SDL_SensorOpen failed: %s", SDL_GetError());
				break;
			}
		}
		if (!gyro_sensor)
			LOGD("no gyroscope sensor found");
	}
}

void touch_overlay_set_game_mode(int in_game)
{
	if (gameplay_active && !in_game) {
		/* Entering menu from gameplay - release game keys, keep stick active
		 * so it can seamlessly switch to arrow key injection */
		release_left_stick_keys();
		if (fire_active) { push_mouse_button(SDL_BUTTON_LEFT, 0); fire_active = 0; }
		rstick_active = 0;
	}
	else if (!gameplay_active && in_game) {
		/* Entering gameplay from menu - release menu keys */
		release_left_stick_keys();
	}
	gameplay_active = in_game;
}

static int hit_hide_button(float x, float y)
{
	float dx = x - HIDE_BTN_CX;
	float dy = y - HIDE_BTN_CY;
	float hit_r = HIDE_BTN_RADIUS * 2.0f; /* generous hit area */
	return (dx*dx + dy*dy < hit_r*hit_r);
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

	/* Hide/show toggle - always active */
	if (event->type == SDL_FINGERDOWN && hit_hide_button(x, y)) {
		touch_hidden = !touch_hidden;
		if (touch_hidden) {
			release_left_stick_keys();
			if (fire_active) { push_mouse_button(SDL_BUTTON_LEFT, 0); fire_active = 0; }
			lstick_active = rstick_active = 0;
			memset(fingers, 0, sizeof(fingers));
		}
		return 1;
	}

	/* When hidden, don't process any other touches */
	if (touch_hidden)
		return 0;

	/* --- Finger down --- */
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
		case ZONE_NONE:
			if (!gameplay_active) {
				/* In menus: tap to confirm (ENTER) */
				push_key_event(SDLK_RETURN, 1);
			}
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

	/* --- Finger up --- */
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
		case ZONE_NONE:
			if (!gameplay_active) {
				push_key_event(SDLK_RETURN, 0);
			}
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

	/* --- Finger motion --- */
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
		/* ZONE_NONE motion is ignored */
		return 1;
	}

	return 0;
}

/* ------------------------------------------------------------------ */
/* Drawing helpers (GLES 1.x)                                         */
/* ------------------------------------------------------------------ */

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

static void draw_hide_button(float sw, float sh)
{
	float bx = HIDE_BTN_CX * sw;
	float by = HIDE_BTN_CY * sh;
	float r = HIDE_BTN_RADIUS * sh;
	float alpha = touch_hidden ? 0.15f : 0.3f;
	draw_filled_circle(bx, by, r, CIRCLE_SEGMENTS, 0.6f, 0.6f, 0.6f, alpha);
	draw_ring(bx, by, r, CIRCLE_SEGMENTS, 0.8f, 0.8f, 0.8f, alpha + 0.15f);
}

void touch_overlay_draw(int screen_w, int screen_h)
{
	/* Update right stick and gyroscope → mouse motion each frame (gameplay only) */
	if (gameplay_active && !touch_hidden) {
		update_right_stick(screen_w, screen_h);
		update_gyroscope(screen_w, screen_h);
	}

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

	/* Always draw the hide/show toggle button */
	draw_hide_button(sw, sh);

	if (touch_hidden)
		goto restore_gl;

	float stick_r_px = STICK_RADIUS * sh;
	float btn_r_px = BTN_RADIUS * sh;

	/* Draw left stick when active */
	if (lstick_active) {
		float cx = lstick_cx * sw;
		float cy = lstick_cy * sh;
		draw_ring(cx, cy, stick_r_px, CIRCLE_SEGMENTS, 1.0f, 1.0f, 1.0f, 0.3f);
		float nx = cx + lstick_dx * stick_r_px;
		float ny = cy + lstick_dy * stick_r_px;
		draw_filled_circle(nx, ny, stick_r_px * 0.3f, CIRCLE_SEGMENTS, 1.0f, 1.0f, 1.0f, 0.5f);
	}

	/* Draw right stick when active (gameplay only) */
	if (gameplay_active && rstick_active) {
		float cx = rstick_cx * sw;
		float cy = rstick_cy * sh;
		draw_ring(cx, cy, stick_r_px, CIRCLE_SEGMENTS, 0.5f, 0.8f, 1.0f, 0.3f);
		float nx = cx + rstick_dx * stick_r_px;
		float ny = cy + rstick_dy * stick_r_px;
		draw_filled_circle(nx, ny, stick_r_px * 0.3f, CIRCLE_SEGMENTS, 0.5f, 0.8f, 1.0f, 0.5f);
	}

	/* Draw fire zone indicator (gameplay only) */
	if (gameplay_active) {
		float alpha = fire_active ? 0.4f : 0.15f;
		float fx = FIRE_ZONE_LEFT * sw;
		float fy1 = FIRE_ZONE_TOP * sh;
		float fy2 = FIRE_ZONE_BOT * sh;
		float fw = (0.92f - FIRE_ZONE_LEFT) * sw;

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

	/* Draw action buttons - always visible */
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

restore_gl:
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
	if (gyro_sensor) {
		SDL_SensorClose(gyro_sensor);
		gyro_sensor = NULL;
	}
}

#endif /* __ANDROID__ */
