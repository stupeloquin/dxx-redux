/*
 *
 * OGL video functions. - Added 9/15/99 Matthew Mueller
 *
 */

#define DECLARE_VARS

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#ifdef _MSC_VER
#include <winsock2.h>
#include <windows.h>
#endif

#if !defined(_MSC_VER) && !defined(macintosh)
#include <unistd.h>
#endif
#if !defined(macintosh)
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#endif

#include <errno.h>
#include <SDL.h>
#include "hudmsg.h"
#include "game.h"
#include "text.h"
#include "gr.h"
#include "gamefont.h"
#include "grdef.h"
#include "palette.h"
#include "u_mem.h"
#include "dxxerror.h"
#include "inferno.h"
#include "screens.h"
#include "strutil.h"
#include "args.h"
#include "key.h"
#include "physfsx.h"
#include "playsave.h"
#include "internal.h"
#include "render.h"
#include "console.h"
#include "config.h"
#include "vers_id.h"
#include "game.h"
#include "pngfile.h"
#include "oglprog.h"

#if defined(__APPLE__) && defined(__MACH__)
#include <OpenGL/glu.h>
#else
#ifndef OGLES
#include <GL/glu.h>
#endif
#endif

#ifdef OGLES
Uint32 sdl_video_flags = SDL_WINDOW_OPENGL;
#else
Uint32 sdl_video_flags = SDL_WINDOW_OPENGL;
#endif
int gr_installed = 0;
SDL_Window *sdl_window = NULL;
SDL_GLContext sdl_gl_context = NULL;
int gl_initialized=0;
int linedotscale=1; // scalar of glLinewidth and glPointSize - only calculated once when resolution changes
int sdl_no_modeswitch=0;

void ogl_swap_buffers_internal(void)
{
	SDL_GL_SwapWindow(sdl_window);
}

int ogl_init_window(int x, int y)
{
	Uint32 use_flags = sdl_video_flags;

	if (gl_initialized)
		ogl_smash_texture_list_internal();

	if (sdl_window) {
#ifndef __ANDROID__
		SDL_SetWindowSize(sdl_window, x, y);
#endif
		SDL_SetWindowTitle(sdl_window, DESCENT_VERSION);
	} else {
		use_flags |= SDL_WINDOW_OPENGL;
		if (sdl_video_flags & SDL_WINDOW_FULLSCREEN)
			use_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;

#ifdef OGLES
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 1);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
#endif

		sdl_window = SDL_CreateWindow(DESCENT_VERSION,
			SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
			x, y, use_flags);
		if (!sdl_window)
			Error("Could not create window: %s\n", SDL_GetError());

		sdl_gl_context = SDL_GL_CreateContext(sdl_window);
		if (!sdl_gl_context)
			Error("Could not create GL context: %s\n", SDL_GetError());
	}

#ifndef OGLES
	glewInit();
#endif
#ifdef OGL_MERGE
	ogl_init_prog();
#endif
	if (Game_wind)
		ogl_cache_level_textures();

	linedotscale = ((x/640<y/480?x/640:y/480)<1?1:(x/640<y/480?x/640:y/480));
	gl_initialized=1;
	return 0;
}

int gr_check_fullscreen(void)
{
	return (sdl_video_flags & SDL_WINDOW_FULLSCREEN)?1:0;
}

int gr_toggle_fullscreen(void)
{
	if (sdl_video_flags & SDL_WINDOW_FULLSCREEN)
		sdl_video_flags &= ~SDL_WINDOW_FULLSCREEN;
	else
		sdl_video_flags |= SDL_WINDOW_FULLSCREEN;

	if (gl_initialized)
	{
		Uint32 flag = (sdl_video_flags & SDL_WINDOW_FULLSCREEN) ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0;
		SDL_SetWindowFullscreen(sdl_window, flag);
	}

	gr_remap_color_fonts();
	gr_remap_mono_fonts();

	if (gl_initialized) // update viewing values for menus
	{
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
#ifdef OGLES
		glOrthof(0.0, 1.0, 0.0, 1.0, -1.0, 1.0);
#else
 		glOrtho(0.0, 1.0, 0.0, 1.0, -1.0, 1.0);
#endif
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();//clear matrix
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		ogl_smash_texture_list_internal();//if we are or were fullscreen, changing vid mode will invalidate current textures
#ifdef OGL_MERGE
		ogl_init_prog();
#endif
		if (Game_wind)
			ogl_cache_level_textures();
	}
	GameCfg.WindowMode = (sdl_video_flags & SDL_WINDOW_FULLSCREEN)?0:1;
	return (sdl_video_flags & SDL_WINDOW_FULLSCREEN)?1:0;
}

static void ogl_init_state(void)
{
	/* select clearing (background) color   */
	glClearColor(0.0, 0.0, 0.0, 0.0);

	/* initialize viewing values */
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
#ifdef OGLES
	glOrthof(0.0, 1.0, 0.0, 1.0, -1.0, 1.0);
#else
 	glOrtho(0.0, 1.0, 0.0, 1.0, -1.0, 1.0);
#endif
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();//clear matrix
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	gr_palette_step_up(0,0,0);//in case its left over from in game

	ogl_init_pixel_buffers(grd_curscreen->sc_w, grd_curscreen->sc_h);
}

// Set the buffer to draw to. 0 is front, 1 is back
void gr_set_draw_buffer(int buf)
{
#ifndef OGLES
	glDrawBuffer((buf == 0) ? GL_FRONT : GL_BACK);
#endif
}

const char *gl_vendor, *gl_renderer, *gl_version, *gl_extensions;

void ogl_get_verinfo(void)
{
#ifndef OGLES
	gl_vendor = (const char *) glGetString (GL_VENDOR);
	gl_renderer = (const char *) glGetString (GL_RENDERER);
	gl_version = (const char *) glGetString (GL_VERSION);
	gl_extensions = (const char *) glGetString (GL_EXTENSIONS);

	con_printf(CON_VERBOSE, "OpenGL: vendor: %s\nOpenGL: renderer: %s\nOpenGL: version: %s\n",gl_vendor,gl_renderer,gl_version);

#ifdef _WIN32
	dglMultiTexCoord2fARB = (glMultiTexCoord2fARB_fp)wglGetProcAddress("glMultiTexCoord2fARB");
	dglActiveTextureARB = (glActiveTextureARB_fp)wglGetProcAddress("glActiveTextureARB");
	dglMultiTexCoord2fSGIS = (glMultiTexCoord2fSGIS_fp)wglGetProcAddress("glMultiTexCoord2fSGIS");
	dglSelectTextureSGIS = (glSelectTextureSGIS_fp)wglGetProcAddress("glSelectTextureSGIS");
#endif

	//add driver specific hacks here.  whee.
	if ((d_stricmp(gl_renderer,"Mesa NVIDIA RIVA 1.0\n")==0 || d_stricmp(gl_renderer,"Mesa NVIDIA RIVA 1.2\n")==0) && d_stricmp(gl_version,"1.2 Mesa 3.0")==0)
	{
		GameArg.DbgGlIntensity4Ok=0;//ignores alpha, always black background instead of transparent.
		GameArg.DbgGlReadPixelsOk=0;//either just returns all black, or kills the X server entirely
		GameArg.DbgGlGetTexLevelParamOk=0;//returns random data..
	}
	if (d_stricmp(gl_vendor,"Matrox Graphics Inc.")==0)
	{
		//displays garbage. reported by
		//  redomen@crcwnet.com (render="Matrox G400" version="1.1.3 5.52.015")
		//  orulz (Matrox G200)
		GameArg.DbgGlIntensity4Ok=0;
	}
#ifdef macintosh
	if (d_stricmp(gl_renderer,"3dfx Voodoo 3")==0) // strangely, includes Voodoo 2
		GameArg.DbgGlGetTexLevelParamOk=0; // Always returns 0
#endif

#ifndef NDEBUG
	con_printf(CON_VERBOSE,"gl_intensity4:%i gl_luminance4_alpha4:%i gl_rgba2:%i gl_readpixels:%i gl_gettexlevelparam:%i\n",GameArg.DbgGlIntensity4Ok,GameArg.DbgGlLuminance4Alpha4Ok,GameArg.DbgGlRGBA2Ok,GameArg.DbgGlReadPixelsOk,GameArg.DbgGlGetTexLevelParamOk);
#endif
	if (!d_stricmp(gl_extensions,"GL_EXT_texture_filter_anisotropic")==0)
	{
		glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &ogl_maxanisotropy);
		con_printf(CON_VERBOSE,"ogl_maxanisotropy:%f\n",ogl_maxanisotropy);
	}
	else if (GameCfg.TexFilt >= 3)
		GameCfg.TexFilt = 2;
#endif
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
		if (mode.w > 0xFFF0 || mode.h > 0xFFF0 || mode.w < 320 || mode.h < 200)
			continue;
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
	char *gr_bm_data;

	if (mode<=0)
		return 0;

	w=SM_W(mode);
	h=SM_H(mode);

	if (!gr_check_mode(mode))
	{
		con_printf(CON_URGENT,"Cannot set %ix%i. Fallback to 640x480\n",w,h);
		w=640;
		h=480;
		Game_screen_mode=mode=SM(w,h);
	}

#ifdef __ANDROID__
	/* On Android, always use native display resolution */
	{
		SDL_DisplayMode dm;
		if (sdl_window) {
			int real_w, real_h;
			SDL_GetWindowSize(sdl_window, &real_w, &real_h);
			if (real_w > 0 && real_h > 0) {
				w = real_w;
				h = real_h;
			}
		} else if (SDL_GetCurrentDisplayMode(0, &dm) == 0) {
			w = dm.w;
			h = dm.h;
		}
		Game_screen_mode = mode = SM(w, h);
	}
#endif

	gr_bm_data=(char *)grd_curscreen->sc_canvas.cv_bitmap.bm_data;//since we use realloc, we want to keep this pointer around.
	memset( grd_curscreen, 0, sizeof(grs_screen));
	grd_curscreen->sc_mode = mode;
	grd_curscreen->sc_w = w;
	grd_curscreen->sc_h = h;
	grd_curscreen->sc_aspect = fixdiv(grd_curscreen->sc_w*GameCfg.AspectX,grd_curscreen->sc_h*GameCfg.AspectY);
	gr_init_canvas(&grd_curscreen->sc_canvas, d_realloc(gr_bm_data,w*h), BM_OGL, w, h);
	gr_set_current_canvas(NULL);

	sdl_video_flags = (sdl_video_flags & ~SDL_WINDOW_BORDERLESS) | (GameCfg.BorderlessWindow ? SDL_WINDOW_BORDERLESS : 0);

	ogl_init_window(w,h);//platform specific code
	ogl_get_verinfo();

	OGL_VIEWPORT(0,0,w,h);
	ogl_init_state();
	gamefont_choose_game_font(w,h);
	gr_remap_color_fonts();
	gr_remap_mono_fonts();

	return 0;
}

#define GLstrcmptestr(a,b) if (d_stricmp(a,#b)==0 || d_stricmp(a,"GL_" #b)==0)return GL_ ## b;
int ogl_atotexfilti(char *a,int min)
{
	GLstrcmptestr(a,NEAREST);
	GLstrcmptestr(a,LINEAR);
	if (min)
	{//mipmaps are valid only for the min filter
		GLstrcmptestr(a,NEAREST_MIPMAP_NEAREST);
		GLstrcmptestr(a,NEAREST_MIPMAP_LINEAR);
		GLstrcmptestr(a,LINEAR_MIPMAP_NEAREST);
		GLstrcmptestr(a,LINEAR_MIPMAP_LINEAR);
	}
	Error("unknown/invalid texture filter %s\n",a);
}

#ifdef _WIN32
char *OglLibPath="opengl32.dll";

int ogl_rt_loaded=0;
int ogl_init_load_library(void)
{
	int retcode=0;
	if (!ogl_rt_loaded)
	{
		retcode = OpenGL_LoadLibrary(true);
		if(retcode)
		{
			if(!glEnd)
			{
				Error("Opengl: Functions not imported\n");
			}
		}
		else
		{
			Error("Opengl: error loading %s\n", OglLibPath);
		}
		ogl_rt_loaded=1;
	}
	return retcode;
}
#endif

void gr_set_attributes(void)
{
#ifndef OGLES
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE,0);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);
	if (GameCfg.Multisample)
	{
		SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
		SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);
	}
	else
	{
		SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
		SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
	}
#endif
	SDL_GL_SetSwapInterval(GameCfg.VSync);
	ogl_smash_texture_list_internal();
	gr_remap_color_fonts();
	gr_remap_mono_fonts();
#ifdef OGL_MERGE
	if (gl_initialized)
		ogl_init_prog();
#endif
	if (Game_wind)
		ogl_cache_level_textures();
}

int gr_init(int mode)
{
	int retcode;

	// Only do this function once!
	if (gr_installed==1)
		return -1;

#ifdef _WIN32
	ogl_init_load_library();
#endif

#ifdef __ANDROID__
	sdl_video_flags|=SDL_WINDOW_FULLSCREEN;
#else
	if (!GameCfg.WindowMode && !GameArg.SysWindow)
		sdl_video_flags|=SDL_WINDOW_FULLSCREEN;

	if (GameArg.SysNoBorders)
		sdl_video_flags|=SDL_WINDOW_BORDERLESS;
#endif

	gr_set_attributes();

	ogl_init_texture_list_internal();

	CALLOC( grd_curscreen,grs_screen,1 );
	grd_curscreen->sc_canvas.cv_bitmap.bm_data = NULL;

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

	ogl_init_pixel_buffers(256, 128);       // for gamefont_init

	gr_installed = 1;

	return 0;
}

void gr_close()
{
	ogl_brightness_r = ogl_brightness_g = ogl_brightness_b = 0;

	if (gl_initialized)
	{
		ogl_smash_texture_list_internal();
	}

	if (grd_curscreen)
	{
		if (grd_curscreen->sc_canvas.cv_bitmap.bm_data)
			d_free(grd_curscreen->sc_canvas.cv_bitmap.bm_data);
		d_free(grd_curscreen);
	}
	ogl_close_pixel_buffers();
#ifdef _WIN32
	if (ogl_rt_loaded)
		OpenGL_LoadLibrary(false);
#endif

	if (sdl_gl_context) { SDL_GL_DeleteContext(sdl_gl_context); sdl_gl_context = NULL; }
	if (sdl_window) { SDL_DestroyWindow(sdl_window); sdl_window = NULL; }
}

extern int r_upixelc;
void ogl_upixelc(int x, int y, int c)
{
	GLfloat vertex_array[] = { (x+grd_curcanv->cv_bitmap.bm_x)/(float)last_width, 1.0-(y+grd_curcanv->cv_bitmap.bm_y)/(float)last_height };
	GLfloat color_array[] = { CPAL2Tr(c), CPAL2Tg(c), CPAL2Tb(c), 1.0, CPAL2Tr(c), CPAL2Tg(c), CPAL2Tb(c), 1.0, CPAL2Tr(c), CPAL2Tg(c), CPAL2Tb(c), 1.0, CPAL2Tr(c), CPAL2Tg(c), CPAL2Tb(c), 1.0 };

	r_upixelc++;
	OGL_DISABLE(TEXTURE_2D);
	glPointSize(linedotscale);
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_COLOR_ARRAY);
	glVertexPointer(2, GL_FLOAT, 0, vertex_array);
	glColorPointer(4, GL_FLOAT, 0, color_array);
	glDrawArrays(GL_POINTS, 0, 1);
	glDisableClientState(GL_VERTEX_ARRAY);
	glDisableClientState(GL_COLOR_ARRAY);
}

unsigned char ogl_ugpixel( grs_bitmap * bitmap, int x, int y )
{
	GLint gl_draw_buffer;
	ubyte buf[4];

#ifndef OGLES
	glGetIntegerv(GL_DRAW_BUFFER, &gl_draw_buffer);
	glReadBuffer(gl_draw_buffer);
#endif

	glReadPixels(bitmap->bm_x + x, SHEIGHT - bitmap->bm_y - y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, buf);

	return gr_find_closest_color(buf[0]/4, buf[1]/4, buf[2]/4);
}

void ogl_urect(int left,int top,int right,int bot)
{
	GLfloat xo, yo, xf, yf, color_r, color_g, color_b, color_a;
	GLfloat color_array[] = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
	GLfloat vertex_array[] = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
	int c=COLOR;

	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_COLOR_ARRAY);

	xo=(left+grd_curcanv->cv_bitmap.bm_x)/(float)last_width;
	xf = (right + 1 + grd_curcanv->cv_bitmap.bm_x) / (float)last_width;
	yo=1.0-(top+grd_curcanv->cv_bitmap.bm_y)/(float)last_height;
	yf = 1.0 - (bot + 1 + grd_curcanv->cv_bitmap.bm_y) / (float)last_height;

	OGL_DISABLE(TEXTURE_2D);

	color_r = CPAL2Tr(c);
	color_g = CPAL2Tg(c);
	color_b = CPAL2Tb(c);

	if (grd_curcanv->cv_fade_level >= GR_FADE_OFF)
		color_a = 1.0;
	else
		color_a = 1.0 - (float)grd_curcanv->cv_fade_level / ((float)GR_FADE_LEVELS - 1.0);

	color_array[0] = color_array[4] = color_array[8] = color_array[12] = color_r;
	color_array[1] = color_array[5] = color_array[9] = color_array[13] = color_g;
	color_array[2] = color_array[6] = color_array[10] = color_array[14] = color_b;
	color_array[3] = color_array[7] = color_array[11] = color_array[15] = color_a;

	vertex_array[0] = xo;
	vertex_array[1] = yo;
	vertex_array[2] = xo;
	vertex_array[3] = yf;
	vertex_array[4] = xf;
	vertex_array[5] = yf;
	vertex_array[6] = xf;
	vertex_array[7] = yo;

	glVertexPointer(2, GL_FLOAT, 0, vertex_array);
	glColorPointer(4, GL_FLOAT, 0, color_array);
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);//replaced GL_QUADS
	glDisableClientState(GL_VERTEX_ARRAY);
}

void ogl_ulinec(int left,int top,int right,int bot,int c)
{
	GLfloat xo,yo,xf,yf;
	GLfloat color_array[] = { CPAL2Tr(c), CPAL2Tg(c), CPAL2Tb(c), (grd_curcanv->cv_fade_level >= GR_FADE_OFF)?1.0:1.0 - (float)grd_curcanv->cv_fade_level / ((float)GR_FADE_LEVELS - 1.0), CPAL2Tr(c), CPAL2Tg(c), CPAL2Tb(c), (grd_curcanv->cv_fade_level >= GR_FADE_OFF)?1.0:1.0 - (float)grd_curcanv->cv_fade_level / ((float)GR_FADE_LEVELS - 1.0), CPAL2Tr(c), CPAL2Tg(c), CPAL2Tb(c), 1.0, CPAL2Tr(c), CPAL2Tg(c), CPAL2Tb(c), (grd_curcanv->cv_fade_level >= GR_FADE_OFF)?1.0:1.0 - (float)grd_curcanv->cv_fade_level / ((float)GR_FADE_LEVELS - 1.0) };
	GLfloat vertex_array[] = { 0.0, 0.0, 0.0, 0.0 };

	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_COLOR_ARRAY);

	xo = (left + grd_curcanv->cv_bitmap.bm_x + 0.5) / (float)last_width;
	xf = (right + grd_curcanv->cv_bitmap.bm_x + 0.5) / (float)last_width;
	yo = 1.0 - (top + grd_curcanv->cv_bitmap.bm_y + 0.5) / (float)last_height;
	yf = 1.0 - (bot + grd_curcanv->cv_bitmap.bm_y + 0.5) / (float)last_height;

	OGL_DISABLE(TEXTURE_2D);

	vertex_array[0] = xo;
	vertex_array[1] = yo;
	vertex_array[2] = xf;
	vertex_array[3] = yf;

	glVertexPointer(2, GL_FLOAT, 0, vertex_array);
	glColorPointer(4, GL_FLOAT, 0, color_array);
	glDrawArrays(GL_LINES, 0, 2);
	glDisableClientState(GL_VERTEX_ARRAY);
	glDisableClientState(GL_COLOR_ARRAY);
}

GLfloat last_r=0, last_g=0, last_b=0;
int do_pal_step=0;

void ogl_do_palfx(void)
{
	GLfloat color_array[] = { last_r, last_g, last_b, 1.0, last_r, last_g, last_b, 1.0, last_r, last_g, last_b, 1.0, last_r, last_g, last_b, 1.0 };
	GLfloat vertex_array[] = { 0,0,0,1,1,1,1,0 };

	OGL_DISABLE(TEXTURE_2D);

	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_COLOR_ARRAY);

	if (do_pal_step)
	{
		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE,GL_ONE);
	}
	else
		return;

	glVertexPointer(2, GL_FLOAT, 0, vertex_array);
	glColorPointer(4, GL_FLOAT, 0, color_array);
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);//replaced GL_QUADS
	glDisableClientState(GL_VERTEX_ARRAY);
	glDisableClientState(GL_COLOR_ARRAY);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

int ogl_brightness_ok = 0;
int ogl_brightness_r = 0, ogl_brightness_g = 0, ogl_brightness_b = 0;
static int old_b_r = 0, old_b_g = 0, old_b_b = 0;

void gr_palette_step_up(int r, int g, int b)
{
	old_b_r = ogl_brightness_r;
	old_b_g = ogl_brightness_g;
	old_b_b = ogl_brightness_b;

	ogl_brightness_r = max(r + gr_palette_gamma, 0);
	ogl_brightness_g = max(g + gr_palette_gamma, 0);
	ogl_brightness_b = max(b + gr_palette_gamma, 0);

	if (!ogl_brightness_ok)
	{
		last_r = ogl_brightness_r / 63.0;
		last_g = ogl_brightness_g / 63.0;
		last_b = ogl_brightness_b / 63.0;

		do_pal_step = (r || g || b || gr_palette_gamma);
	}
	else
	{
		do_pal_step = 0;
	}
}

#undef min
static inline int min(int x, int y) { return x < y ? x : y; }

void gr_palette_load( ubyte *pal )
{
	int i;

	for (i=0; i<768; i++ )
	{
		gr_current_pal[i] = pal[i];
		if (gr_current_pal[i] > 63)
			gr_current_pal[i] = 63;
	}

	gr_palette_step_up(0, 0, 0); // make ogl_setbrightness_internal get run so that menus get brightened too.
	init_computed_colors();
}

void gr_palette_read(ubyte * pal)
{
	int i;
	for (i=0; i<768; i++ )
	{
		pal[i]=gr_current_pal[i];
		if (pal[i] > 63)
			pal[i] = 63;
	}
}

#define GL_BGR_EXT 0x80E0

typedef struct
{
      unsigned char TGAheader[12];
      unsigned char header[6];
} TGA_header;

//writes out an uncompressed RGB .tga file
//if we got really spiffy, we could optionally link in libpng or something, and use that.
void write_bmp(char *savename,int w,int h)
{
	unsigned char *rgbaBuf, *buf;
	#ifdef HAVE_LIBPNG
	png_data pdata;
	int x, y;
	#else
	TGA_header TGA;
	GLbyte HeightH,HeightL,WidthH,WidthL;
	PHYSFS_File *TGAFile;
	int pixel;
	#endif

	buf = (unsigned char*)d_malloc(w * h * 3);

	rgbaBuf = (unsigned char*) d_malloc(w * h * 4);
	glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgbaBuf);
	#ifdef HAVE_LIBPNG
	for(y = 0; y < h; y++) {
		int wofs = y * w * 3, rofs = (h - 1 - y) * w * 4;
		for(x = 0; x < w; x++) {
			*(buf + wofs + x * 3) = *(rgbaBuf + rofs + x * 4);
			*(buf + wofs + x * 3 + 1) = *(rgbaBuf + rofs + x * 4 + 1);
			*(buf + wofs + x * 3 + 2) = *(rgbaBuf + rofs + x * 4 + 2);
		}
	}
	#else
	for(pixel = 0; pixel < w * h; pixel++) {
		*(buf + pixel * 3) = *(rgbaBuf + pixel * 4 + 2);
		*(buf + pixel * 3 + 1) = *(rgbaBuf + pixel * 4 + 1);
		*(buf + pixel * 3 + 2) = *(rgbaBuf + pixel * 4);
	}
	#endif
	d_free(rgbaBuf);

	#ifdef HAVE_LIBPNG
	memset(&pdata, 0, sizeof(pdata));
	pdata.width = w;
	pdata.height = h;
	pdata.data = buf;
	pdata.depth = 8;

	if (!write_png(savename, &pdata))
		con_printf(CON_URGENT,"Could not create PNG file to dump screenshot!");
	#else
	if (!(TGAFile = PHYSFSX_openWriteBuffered(savename)))
	{
		con_printf(CON_URGENT,"Could not create TGA file to dump screenshot!");
		d_free(buf);
		return;
	}

	HeightH = (GLbyte)(h / 256);
	HeightL = (GLbyte)(h % 256);
	WidthH  = (GLbyte)(w / 256);
	WidthL  = (GLbyte)(w % 256);
	// Write TGA Header
	TGA.TGAheader[0] = 0;
	TGA.TGAheader[1] = 0;
	TGA.TGAheader[2] = 2;
	TGA.TGAheader[3] = 0;
	TGA.TGAheader[4] = 0;
	TGA.TGAheader[5] = 0;
	TGA.TGAheader[6] = 0;
	TGA.TGAheader[7] = 0;
	TGA.TGAheader[8] = 0;
	TGA.TGAheader[9] = 0;
	TGA.TGAheader[10] = 0;
	TGA.TGAheader[11] = 0;
	TGA.header[0] = (GLbyte) WidthL;
	TGA.header[1] = (GLbyte) WidthH;
	TGA.header[2] = (GLbyte) HeightL;
	TGA.header[3] = (GLbyte) HeightH;
	TGA.header[4] = (GLbyte) 24;
	TGA.header[5] = 0;
	PHYSFS_write(TGAFile,&TGA,sizeof(TGA_header),1);
	PHYSFS_write(TGAFile,buf,w*h*3*sizeof(unsigned char),1);
	PHYSFS_close(TGAFile);
	#endif

	d_free(buf);
}

void save_screen_shot(int automap_flag)
{
	static int savenum=0;
	char savename[13+sizeof(SCRNS_DIR)];
	#ifdef HAVE_LIBPNG
	const char *ext = "png";
	#else
	const char *ext = "tga";
	#endif

	if (!GameArg.DbgGlReadPixelsOk){
		if (!automap_flag)
			HUD_init_message_literal(HM_DEFAULT, "glReadPixels not supported on your configuration");
		return;
	}

	stop_time();

	if (!PHYSFSX_exists(SCRNS_DIR,0))
		PHYSFS_mkdir(SCRNS_DIR); //try making directory

	do
	{
		sprintf(savename, "%sscrn%04d.%s", SCRNS_DIR, savenum++, ext);
	} while (PHYSFSX_exists(savename,0));

	if (!automap_flag)
		HUD_init_message(HM_DEFAULT, "%s '%s'", TXT_DUMPING_SCREEN, savename + strlen(SCRNS_DIR));

#ifndef OGLES
	glReadBuffer(GL_FRONT);
#endif

	write_bmp(savename,grd_curscreen->sc_w,grd_curscreen->sc_h);

	start_time();
}
