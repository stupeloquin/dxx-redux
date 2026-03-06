/*
 *
 * SDL video functions.
 *
 */

#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <math.h>
#include <SDL.h>
#include "gr.h"
#include "grdef.h"
#include "palette.h"
#include "u_mem.h"
#include "dxxerror.h"
#include "vers_id.h"
#include "gamefont.h"
#include "args.h"
#include "config.h"

Uint32 sdl_video_flags = 0;
extern SDL_Window *sdl_window;
static SDL_Renderer *sdl_renderer = NULL;
static SDL_Texture *sdl_texture = NULL;
SDL_Surface *screen,*canvas;
int gr_installed = 0;

void gr_flip()
{
	SDL_UpdateTexture(sdl_texture, NULL, canvas->pixels, canvas->pitch);
	SDL_RenderClear(sdl_renderer);
	SDL_RenderCopy(sdl_renderer, sdl_texture, NULL, NULL);
	SDL_RenderPresent(sdl_renderer);
}

// Set the buffer to draw to. 0 is front, 1 is back
// With SDL, can't use it without resetting the video mode
void gr_set_draw_buffer(int buf)
{
	buf = buf;
}

// returns possible (fullscreen) resolutions if any.
int gr_list_modes( u_int32_t gsmodes[] )
{
	int modesnum = 0;
	int num_modes = SDL_GetNumDisplayModes(0);
	if (num_modes < 1) return 0;
	for (int i = 0; i < num_modes; i++) {
		SDL_DisplayMode mode;
		if (SDL_GetDisplayMode(0, i, &mode) != 0) continue;
		if (mode.w > 0xFFF0 || mode.h > 0xFFF0 || mode.w < 320 || mode.h < 200) continue;
		gsmodes[modesnum] = SM(mode.w, mode.h);
		modesnum++;
		if (modesnum >= 50) break;
	}
	return modesnum;
}

int gr_check_mode(u_int32_t mode)
{
	return 32;
}

int gr_set_mode(u_int32_t mode)
{
	unsigned int w, h;
	if (mode<=0) return 0;
	w=SM_W(mode);
	h=SM_H(mode);

	Uint32 flags = 0;
	if (sdl_video_flags & SDL_WINDOW_FULLSCREEN)
		flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
	if (GameCfg.BorderlessWindow)
		flags |= SDL_WINDOW_BORDERLESS;

	if (sdl_window) {
		SDL_SetWindowSize(sdl_window, w, h);
	} else {
		sdl_window = SDL_CreateWindow(DESCENT_VERSION,
			SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, flags);
		if (!sdl_window) { Error("Could not create window\n"); exit(1); }
	}

	if (sdl_renderer) SDL_DestroyRenderer(sdl_renderer);
	sdl_renderer = SDL_CreateRenderer(sdl_window, -1, SDL_RENDERER_ACCELERATED);
	if (!sdl_renderer) sdl_renderer = SDL_CreateRenderer(sdl_window, -1, 0);
	if (!sdl_renderer) { Error("Could not create renderer\n"); exit(1); }

	if (sdl_texture) SDL_DestroyTexture(sdl_texture);
	sdl_texture = SDL_CreateTexture(sdl_renderer, SDL_PIXELFORMAT_RGB332, SDL_TEXTUREACCESS_STREAMING, w, h);
	if (!sdl_texture) { Error("Could not create texture\n"); exit(1); }

	if (canvas) SDL_FreeSurface(canvas);
	canvas = SDL_CreateRGBSurface(0, w, h, 8, 0, 0, 0, 0);
	if (canvas == NULL) { Error("Could not create canvas surface\n"); exit(1); }

	memset(grd_curscreen, 0, sizeof(grs_screen));
	grd_curscreen->sc_mode = mode;
	grd_curscreen->sc_w = w;
	grd_curscreen->sc_h = h;
	grd_curscreen->sc_aspect = fixdiv(grd_curscreen->sc_w*GameCfg.AspectX,grd_curscreen->sc_h*GameCfg.AspectY);
	gr_init_canvas(&grd_curscreen->sc_canvas, canvas->pixels, BM_LINEAR, w, h);
	window_update_canvases();
	gr_set_current_canvas(NULL);
	SDL_ShowCursor(0);
	gamefont_choose_game_font(w,h);
	gr_palette_load(gr_palette);
	gr_remap_color_fonts();
	gr_remap_mono_fonts();
	return 0;
}

int gr_check_fullscreen(void)
{
	return (sdl_video_flags & SDL_WINDOW_FULLSCREEN)?1:0;
}

int gr_toggle_fullscreen(void)
{
	gr_remap_color_fonts();
	gr_remap_mono_fonts();
	sdl_video_flags ^= SDL_WINDOW_FULLSCREEN;
	if (sdl_window)
		SDL_SetWindowFullscreen(sdl_window, (sdl_video_flags & SDL_WINDOW_FULLSCREEN) ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
	GameCfg.WindowMode = (sdl_video_flags & SDL_WINDOW_FULLSCREEN)?0:1;
	return (sdl_video_flags & SDL_WINDOW_FULLSCREEN)?1:0;
}

void gr_set_attributes(void)
{
}

int gr_init(int mode)
{
	int retcode;

	// Only do this function once!
	if (gr_installed==1)
		return -1;

	if (SDL_Init(SDL_INIT_VIDEO) < 0)
	{
		Error("SDL library video initialisation failed: %s.",SDL_GetError());
	}

	CALLOC( grd_curscreen,grs_screen,1 );

	if (!GameCfg.WindowMode && !GameArg.SysWindow)
		sdl_video_flags|=SDL_WINDOW_FULLSCREEN;

	if (GameArg.SysNoBorders)
		sdl_video_flags|=SDL_WINDOW_BORDERLESS;

	// Set the mode.
	if ((retcode=gr_set_mode(mode)))
		return retcode;

	grd_curscreen->sc_canvas.cv_color = 0;
	grd_curscreen->sc_canvas.cv_fade_level = GR_FADE_OFF;
	grd_curscreen->sc_canvas.cv_blend_func = GR_BLEND_NORMAL;
	grd_curscreen->sc_canvas.cv_drawmode = 0;
	grd_curscreen->sc_canvas.cv_font = NULL;
	grd_curscreen->sc_canvas.cv_font_fg_color = 0;
	grd_curscreen->sc_canvas.cv_font_bg_color = 0;
	gr_set_current_canvas( &grd_curscreen->sc_canvas );

	gr_installed = 1;

	return 0;
}

void gr_close()
{
	if (gr_installed==1)
	{
		gr_installed = 0;
		if (sdl_texture) { SDL_DestroyTexture(sdl_texture); sdl_texture = NULL; }
		if (sdl_renderer) { SDL_DestroyRenderer(sdl_renderer); sdl_renderer = NULL; }
		d_free(grd_curscreen);
		SDL_ShowCursor(1);
		SDL_FreeSurface(canvas);
	}
}

// Palette functions follow.
static int last_r=0, last_g=0, last_b=0;

void gr_palette_step_up( int r, int g, int b )
{
	int i;
	ubyte *p = gr_palette;
	int temp;
	SDL_Palette *palette;
	SDL_Color colors[256];

	if ( (r==last_r) && (g==last_g) && (b==last_b) )
		return;

	last_r = r;
	last_g = g;
	last_b = b;

	palette = canvas->format->palette;

	if (palette == NULL)
		return; // Display is not palettised

	for (i=0; i<256; i++)
	{
		temp = (int)(*p++) + r + gr_palette_gamma;

		if (temp<0)
			temp=0;
		else if (temp>63)
			temp=63;

		colors[i].r = temp * 4;
		temp = (int)(*p++) + g + gr_palette_gamma;

		if (temp<0)
			temp=0;
		else if (temp>63)
			temp=63;

		colors[i].g = temp * 4;
		temp = (int)(*p++) + b + gr_palette_gamma;

		if (temp<0)
			temp=0;
		else if (temp>63)
			temp=63;

		colors[i].b = temp * 4;
	}

	SDL_SetPaletteColors(canvas->format->palette, colors, 0, 256);
}

#undef min
static inline int min(int x, int y) { return x < y ? x : y; }

void gr_palette_load( ubyte *pal )
{
	int i, j;
	SDL_Palette *palette;
	SDL_Color colors[256];
	ubyte gamma[64];

	if (memcmp(pal,gr_current_pal,768))
		SDL_FillRect(canvas, NULL, SDL_MapRGB(canvas->format, 0, 0, 0));

	for (i=0; i<768; i++ )
	{
		gr_current_pal[i] = pal[i];
		if (gr_current_pal[i] > 63)
			gr_current_pal[i] = 63;
	}

	if (canvas == NULL)
		return;

	palette = canvas->format->palette;

	if (palette == NULL)
		return; // Display is not palettised

	for (i=0;i<64;i++)
		gamma[i] = (int)((pow(((double)(14)/(double)(32)), 1.0)*i) + 0.5);

	for (i = 0, j = 0; j < 256; j++)
	{
		int c;
		c = gr_find_closest_color(gamma[gr_palette[j*3]],gamma[gr_palette[j*3+1]],gamma[gr_palette[j*3+2]]);
		gr_fade_table[14*256+j] = c;
		colors[j].r = (min(gr_current_pal[i++] + gr_palette_gamma, 63)) * 4;
		colors[j].g = (min(gr_current_pal[i++] + gr_palette_gamma, 63)) * 4;
		colors[j].b = (min(gr_current_pal[i++] + gr_palette_gamma, 63)) * 4;
	}

	SDL_SetPaletteColors(canvas->format->palette, colors, 0, 256);
	init_computed_colors();
	gr_remap_color_fonts();
	gr_remap_mono_fonts();
}

void gr_palette_read(ubyte * pal)
{
	SDL_Palette *palette;
	int i, j;

	palette = canvas->format->palette;

	if (palette == NULL)
		return; // Display is not palettised

	for (i = 0, j=0; i < 256; i++)
	{
		pal[j++] = palette->colors[i].r / 4;
		pal[j++] = palette->colors[i].g / 4;
		pal[j++] = palette->colors[i].b / 4;
	}
}
