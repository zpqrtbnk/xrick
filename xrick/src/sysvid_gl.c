/*
 * xrick/src/sysvid_gl.c
 *
 * OpenGL shader chain between the game's frame and the screen (ENABLE_SHADERS, config.h).
 *
 * sysvid.c converts the 8-bit frame buffer to RGBA as before and hands the whole frame
 * to sysvid_gl_present, which uploads it and runs it through the passes of `chain`
 * below: each pass renders into an offscreen texture (FBO) that the next pass samples,
 * and the last pass renders into the window, letterboxed like SDL_Renderer's
 * SDL_LOGICAL_PRESENTATION_LETTERBOX.
 *
 * Passes are libretro glsl-shaders sources (.glsl, both stages in one file split by
 * #if defined(VERTEX) / FRAGMENT), compiled in from src/shaders/ (see embed.sh there),
 * and are fed what RetroArch's GLSL driver feeds them (gfx/drivers_shader/shader_glsl.c):
 *   attributes  VertexCoord, TexCoord (and OrigTexCoord, PassNTexCoord, ...)
 *   uniforms    MVPMatrix, FrameCount, FrameDirection, InputSize, TextureSize,
 *               OutputSize, FinalViewportSize, Texture (the previous pass's output)
 *   earlier     Orig* = the game frame; Pass<j+1>* = PassPrev<p-j>* = output of pass j;
 *               PassPrev<p+1>* = the game frame, for pass p (0-based)
 * Not supported: #pragma parameter uniforms (PARAMETER_UNIFORM stays undefined, so the
 * shaders use their built-in defaults), LUT textures, frame history (Prev*), feedback,
 * float/sRGB framebuffers, mipmaps. Every texture is exactly its image size, so
 * TextureSize == InputSize and all texture coordinates run 0..1.
 *
 * Desktop: GL 3.3 core, functions loaded through SDL_GL_GetProcAddress (nothing to link).
 * Web: WebGL 2 (GLES 3.0), called directly; needs -sMAX_WEBGL_VERSION=2 at link.
 */

#include "config.h"

#ifdef ENABLE_SHADERS

#include <stdio.h>  /* snprintf */
#include <string.h> /* strstr, strchr */

#include <SDL3/SDL.h>
#ifdef __EMSCRIPTEN__
#include <GLES3/gl3.h>
#else
#include <SDL3/SDL_opengl.h>
#endif

#include "system.h"
#include "sysvid_gl.h"



/*
 * the chain: passes run top to bottom. mirrors a .glslp preset:
 *   filter_linear  filter_linearN -- how this pass samples its input (Texture)
 *   scale_type     scale_typeN    -- size of this pass's output:
 *                    SCALE_SOURCE   input size * scale
 *                    SCALE_VIEWPORT letterboxed window area * scale
 *                    SCALE_ABSOLUTE scale pixels
 *   max_scale      not in .glslp -- if > 0, the output is at most input size *
 *                  max_scale, whatever scale_type gives (aspect kept)
 * the last pass always renders straight into the window at the letterboxed size;
 * its scale_type/scale/max_scale are not used.
 * to add a shader: put its .glsl under src/shaders/, run embed.sh, add its passes here.
 */
enum { SCALE_SOURCE, SCALE_VIEWPORT, SCALE_ABSOLUTE };

typedef struct {
	const char *name;
	const char *source;
	int filter_linear;
	int scale_type;
	float scale;
	float max_scale;
} shader_pass_t;

/* src/shaders/xbrz/xbrz-freescale-multipass.glslp */
static const char xbrz_freescale_pass0[] =
#include "shaders/xbrz/xbrz-freescale-pass0.glsl.inc"
;
static const char xbrz_freescale_pass1[] =
#include "shaders/xbrz/xbrz-freescale-pass1.glsl.inc"
;
/* src/shaders/stock.glsl: plain copy */
static const char stock[] =
#include "shaders/stock.glsl.inc"
;

/*
 * xBRZ draws its curves at the size of its output, so at a large zoom they get very
 * smooth and the pixel art looks like vector art. its output is capped at
 * XBRZ_MAX_SCALE x the game frame, and the last pass stretches that to the window with
 * linear filtering: up to that zoom the picture is plain xBRZ (the copy is 1:1), above
 * it the curves keep that level of detail and soften instead.
 */
#define XBRZ_MAX_SCALE 3.0f

static const shader_pass_t chain[] = {
	{ "xbrz-freescale-pass0", xbrz_freescale_pass0, 0, SCALE_SOURCE, 1.0f, 0 },
	{ "xbrz-freescale-pass1", xbrz_freescale_pass1, 0, SCALE_VIEWPORT, 1.0f, XBRZ_MAX_SCALE },
	{ "stock", stock, 1, SCALE_VIEWPORT, 1.0f, 0 },
};
#define NPASSES ((int)(sizeof(chain) / sizeof(chain[0])))



/*
 * GL entry points. desktop: pointers loaded from SDL_GL_GetProcAddress, called as
 * GL(glFoo)(...); web: the functions themselves.
 */
#define GL_FUNCS(X) \
	X(void,   glActiveTexture, (GLenum texture)) \
	X(void,   glAttachShader, (GLuint program, GLuint shader)) \
	X(void,   glBindBuffer, (GLenum target, GLuint buffer)) \
	X(void,   glBindFramebuffer, (GLenum target, GLuint framebuffer)) \
	X(void,   glBindTexture, (GLenum target, GLuint texture)) \
	X(void,   glBindVertexArray, (GLuint array)) \
	X(void,   glBufferData, (GLenum target, GLsizeiptr size, const void *data, GLenum usage)) \
	X(GLenum, glCheckFramebufferStatus, (GLenum target)) \
	X(void,   glClear, (GLbitfield mask)) \
	X(void,   glClearColor, (GLfloat r, GLfloat g, GLfloat b, GLfloat a)) \
	X(void,   glColorMask, (GLboolean r, GLboolean g, GLboolean b, GLboolean a)) \
	X(void,   glCompileShader, (GLuint shader)) \
	X(GLuint, glCreateProgram, (void)) \
	X(GLuint, glCreateShader, (GLenum type)) \
	X(void,   glDeleteBuffers, (GLsizei n, const GLuint *buffers)) \
	X(void,   glDeleteFramebuffers, (GLsizei n, const GLuint *framebuffers)) \
	X(void,   glDeleteProgram, (GLuint program)) \
	X(void,   glDeleteShader, (GLuint shader)) \
	X(void,   glDeleteTextures, (GLsizei n, const GLuint *textures)) \
	X(void,   glDeleteVertexArrays, (GLsizei n, const GLuint *arrays)) \
	X(void,   glDisableVertexAttribArray, (GLuint index)) \
	X(void,   glDrawArrays, (GLenum mode, GLint first, GLsizei count)) \
	X(void,   glEnableVertexAttribArray, (GLuint index)) \
	X(void,   glFramebufferTexture2D, (GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level)) \
	X(void,   glGenBuffers, (GLsizei n, GLuint *buffers)) \
	X(void,   glGenFramebuffers, (GLsizei n, GLuint *framebuffers)) \
	X(void,   glGenTextures, (GLsizei n, GLuint *textures)) \
	X(void,   glGenVertexArrays, (GLsizei n, GLuint *arrays)) \
	X(GLint,  glGetAttribLocation, (GLuint program, const GLchar *name)) \
	X(void,   glGetProgramInfoLog, (GLuint program, GLsizei size, GLsizei *length, GLchar *log)) \
	X(void,   glGetProgramiv, (GLuint program, GLenum pname, GLint *params)) \
	X(void,   glGetShaderInfoLog, (GLuint shader, GLsizei size, GLsizei *length, GLchar *log)) \
	X(void,   glGetShaderiv, (GLuint shader, GLenum pname, GLint *params)) \
	X(GLint,  glGetUniformLocation, (GLuint program, const GLchar *name)) \
	X(void,   glLinkProgram, (GLuint program)) \
	X(void,   glPixelStorei, (GLenum pname, GLint param)) \
	X(void,   glShaderSource, (GLuint shader, GLsizei count, const GLchar *const *string, const GLint *length)) \
	X(void,   glTexImage2D, (GLenum target, GLint level, GLint internalformat, GLsizei w, GLsizei h, GLint border, GLenum format, GLenum type, const void *pixels)) \
	X(void,   glTexParameteri, (GLenum target, GLenum pname, GLint param)) \
	X(void,   glTexSubImage2D, (GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, const void *pixels)) \
	X(void,   glUniform1i, (GLint location, GLint v0)) \
	X(void,   glUniform2f, (GLint location, GLfloat v0, GLfloat v1)) \
	X(void,   glUniformMatrix4fv, (GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)) \
	X(void,   glUseProgram, (GLuint program)) \
	X(void,   glVertexAttrib4f, (GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w)) \
	X(void,   glVertexAttribPointer, (GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer)) \
	X(void,   glViewport, (GLint x, GLint y, GLsizei w, GLsizei h))

#ifdef __EMSCRIPTEN__
#define GL(f) f
#define GLSL_VERSION "#version 300 es\n"
#else
#define GL(f) p_##f
#define GLSL_VERSION "#version 330 core\n"
#define X(ret, name, args) typedef ret (APIENTRY *name##_t) args; static name##_t p_##name;
GL_FUNCS(X)
#undef X
#endif

/* GL 3.3 core and GLES 3.0 both guarantee at least 16 vertex attributes and 16
   fragment texture units */
#define MAX_ATTRIBS 16
#define MAX_UNITS 16



typedef struct {
	GLuint prog;
	GLuint fbo, tex; /* output, all passes but the last */
	int w, h;        /* output size */
} pass_t;

static SDL_Window *window;
static SDL_GLContext context;
static pass_t passes[NPASSES];
static GLuint vao, vbo, orig; /* orig: the game frame */
static int orig_w, orig_h;
static int frame_count;

/* the full-screen quad as a triangle strip. VertexCoord is (x, y, 0, 1) and the same
   buffer, read two floats at a time, is every *TexCoord: texture t = 0 is the frame's
   top row. FBO passes map y = 0 to their row 0, so every intermediate texture keeps
   that orientation; only the pass into the window flips (see mvp) */
static const GLfloat quad[] = {
	0, 0, 0, 1,
	1, 0, 0, 1,
	0, 1, 0, 1,
	1, 1, 0, 1,
};

/* column-major ortho(0,1,0,1): y up into an FBO, y down into the window */
static const GLfloat mvp_fbo[16]    = { 2, 0, 0, 0,  0, 2, 0, 0,  0, 0, -1, 0,  -1, -1, 0, 1 };
static const GLfloat mvp_window[16] = { 2, 0, 0, 0,  0, -2, 0, 0,  0, 0, -1, 0,  -1, 1, 0, 1 };



/*
 * compile one stage of a libretro .glsl: our #version, then VERTEX or FRAGMENT, then
 * the source. a #version the source carries itself is dropped, as RetroArch does.
 */
static GLuint compile(const shader_pass_t *p, GLenum type)
{
	const GLchar *src[3];
	const char *body = p->source;
	const char *v = strstr(body, "#version");
	GLuint sh;
	GLint ok;
	char log[1024];

	if (v)
	{
		const char *eol = strchr(v, '\n');
		body = eol ? eol + 1 : v + strlen(v);
	}
	src[0] = GLSL_VERSION;
	src[1] = type == GL_VERTEX_SHADER ? "#define VERTEX\n" : "#define FRAGMENT\n";
	src[2] = body;

	sh = GL(glCreateShader)(type);
	GL(glShaderSource)(sh, 3, src, NULL);
	GL(glCompileShader)(sh);
	GL(glGetShaderiv)(sh, GL_COMPILE_STATUS, &ok);
	if (!ok)
	{
		GL(glGetShaderInfoLog)(sh, sizeof(log), NULL, log);
		sys_printf("xrick/video: %s: %s shader: %s\n", p->name,
			type == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
		GL(glDeleteShader)(sh);
		return 0;
	}
	return sh;
}

static GLuint build_program(const shader_pass_t *p)
{
	GLuint vs, fs, prog;
	GLint ok;
	char log[1024];

	if (!(vs = compile(p, GL_VERTEX_SHADER)))
		return 0;
	if (!(fs = compile(p, GL_FRAGMENT_SHADER)))
	{
		GL(glDeleteShader)(vs);
		return 0;
	}
	prog = GL(glCreateProgram)();
	GL(glAttachShader)(prog, vs);
	GL(glAttachShader)(prog, fs);
	GL(glLinkProgram)(prog);
	GL(glDeleteShader)(vs); /* freed with the program */
	GL(glDeleteShader)(fs);
	GL(glGetProgramiv)(prog, GL_LINK_STATUS, &ok);
	if (!ok)
	{
		GL(glGetProgramInfoLog)(prog, sizeof(log), NULL, log);
		sys_printf("xrick/video: %s: link: %s\n", p->name, log);
		GL(glDeleteProgram)(prog);
		return 0;
	}
	return prog;
}



static void set_filter(GLuint tex, int linear)
{
	GLint f = linear ? GL_LINEAR : GL_NEAREST;

	GL(glBindTexture)(GL_TEXTURE_2D, tex);
	GL(glTexParameteri)(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, f);
	GL(glTexParameteri)(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, f);
}

static GLuint new_texture(int w, int h)
{
	GLuint tex;

	GL(glGenTextures)(1, &tex);
	GL(glBindTexture)(GL_TEXTURE_2D, tex);
	GL(glTexParameteri)(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	GL(glTexParameteri)(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	GL(glTexImage2D)(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	set_filter(tex, 0);
	return tex;
}

/* (re)allocate pass <p>'s output when its size changes (window zoom, fullscreen) */
static int size_output(pass_t *p, int w, int h)
{
	if (p->fbo && p->w == w && p->h == h)
		return 1;
	if (p->tex)
		GL(glDeleteTextures)(1, &p->tex);
	if (!p->fbo)
		GL(glGenFramebuffers)(1, &p->fbo);
	p->tex = new_texture(w, h);
	p->w = w;
	p->h = h;
	GL(glBindFramebuffer)(GL_FRAMEBUFFER, p->fbo);
	GL(glFramebufferTexture2D)(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, p->tex, 0);
	if (GL(glCheckFramebufferStatus)(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
	{
		sys_printf("xrick/video: %dx%d framebuffer incomplete\n", w, h);
		return 0;
	}
	return 1;
}



/*
 * bind texture <tex> (w x h) to the <prefix>Texture, <prefix>TextureSize,
 * <prefix>InputSize uniforms and <prefix>TexCoord attribute of <prog>, if it has them
 */
static void bind_frame(GLuint prog, const char *prefix, GLuint tex, int w, int h, GLuint *unit)
{
	char name[64];
	GLint loc;

	snprintf(name, sizeof(name), "%sTexture", prefix);
	if ((loc = GL(glGetUniformLocation)(prog, name)) >= 0 && *unit < MAX_UNITS)
	{
		GL(glActiveTexture)(GL_TEXTURE0 + *unit);
		GL(glBindTexture)(GL_TEXTURE_2D, tex);
		GL(glUniform1i)(loc, (GLint)*unit);
		(*unit)++;
	}
	snprintf(name, sizeof(name), "%sTextureSize", prefix);
	if ((loc = GL(glGetUniformLocation)(prog, name)) >= 0)
		GL(glUniform2f)(loc, (GLfloat)w, (GLfloat)h);
	snprintf(name, sizeof(name), "%sInputSize", prefix);
	if ((loc = GL(glGetUniformLocation)(prog, name)) >= 0)
		GL(glUniform2f)(loc, (GLfloat)w, (GLfloat)h);
	snprintf(name, sizeof(name), "%sTexCoord", prefix);
	if ((loc = GL(glGetAttribLocation)(prog, name)) >= 0)
	{
		GL(glEnableVertexAttribArray)((GLuint)loc);
		GL(glVertexAttribPointer)((GLuint)loc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), NULL);
	}
}



/*
 * sysvid_gl_init
 *
 * see sysvid_gl.h
 */
SDL_Window *sysvid_gl_init(const char *title, int win_w, int win_h, SDL_WindowFlags flags,
	int fb_w, int fb_h)
{
	int i;

	orig_w = fb_w;
	orig_h = fb_h;

	SDL_GL_ResetAttributes();
#ifdef __EMSCRIPTEN__
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3); /* WebGL 2 */
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
#ifdef __APPLE__
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#endif
#endif
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

	window = SDL_CreateWindow(title, win_w, win_h, flags | SDL_WINDOW_OPENGL);
	if (!window)
		goto fail;
	context = SDL_GL_CreateContext(window);
	if (!context)
		goto fail;

#ifndef __EMSCRIPTEN__
#define X(ret, name, args) \
	if (!(p_##name = (name##_t)SDL_GL_GetProcAddress(#name))) \
	{ \
		sys_printf("xrick/video: no GL function " #name "\n"); \
		goto fail; \
	}
	GL_FUNCS(X)
#undef X
	/* no vsync, like the SDL_Renderer path: the game paces itself. not on the web,
	   where SDL maps the swap interval onto the main loop's timing mode */
	SDL_GL_SetSwapInterval(0);
#endif

	for (i = 0; i < NPASSES; i++)
		if (!(passes[i].prog = build_program(&chain[i])))
			goto fail;

	GL(glGenVertexArrays)(1, &vao);
	GL(glBindVertexArray)(vao);
	GL(glGenBuffers)(1, &vbo);
	GL(glBindBuffer)(GL_ARRAY_BUFFER, vbo);
	GL(glBufferData)(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);

	orig = new_texture(orig_w, orig_h);
	GL(glPixelStorei)(GL_UNPACK_ALIGNMENT, 4);

	return window; /* silent on success; failures above are logged */

fail:
	sys_printf("xrick/video: shaders unavailable (%s), falling back to SDL_Renderer\n", SDL_GetError());
	sysvid_gl_shutdown();
	return NULL;
}



/*
 * sysvid_gl_present
 *
 * see sysvid_gl.h
 */
void sysvid_gl_present(const Uint8 *rgba)
{
	int dw, dh, vw, vh, vx, vy, in_w, in_h, i;
	GLuint in_tex;

	/* letterboxed viewport: the frame scaled to fit the window, aspect kept */
	SDL_GetWindowSizeInPixels(window, &dw, &dh);
	if (dw <= 0 || dh <= 0)
		return;
	if ((long)dw * orig_h <= (long)dh * orig_w)
	{
		vw = dw;
		vh = (int)((long)dw * orig_h / orig_w);
	}
	else
	{
		vh = dh;
		vw = (int)((long)dh * orig_w / orig_h);
	}
	vx = (dw - vw) / 2;
	vy = (dh - vh) / 2;

	GL(glActiveTexture)(GL_TEXTURE0);
	GL(glBindTexture)(GL_TEXTURE_2D, orig);
	GL(glTexSubImage2D)(GL_TEXTURE_2D, 0, 0, 0, orig_w, orig_h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);

	GL(glBindVertexArray)(vao);
	GL(glBindBuffer)(GL_ARRAY_BUFFER, vbo);

	in_tex = orig;
	in_w = orig_w;
	in_h = orig_h;
	for (i = 0; i < NPASSES; i++)
	{
		const shader_pass_t *c = &chain[i];
		pass_t *p = &passes[i];
		int last = i == NPASSES - 1;
		int ow, oh, j;
		GLuint unit, a;
		GLint loc;
		char name[32];

		if (last)
		{
			ow = vw;
			oh = vh;
			GL(glBindFramebuffer)(GL_FRAMEBUFFER, 0);
			GL(glViewport)(0, 0, dw, dh);
			GL(glClearColor)(0, 0, 0, 1);
			GL(glClear)(GL_COLOR_BUFFER_BIT);
			GL(glViewport)(vx, vy, vw, vh);
			/* the window stays opaque whatever alpha the shader writes: on the web
			   the canvas has an alpha channel (see sysvid_setDisplayPalette) */
			GL(glColorMask)(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
		}
		else
		{
			switch (c->scale_type)
			{
			case SCALE_VIEWPORT:
				ow = (int)((float)vw * c->scale);
				oh = (int)((float)vh * c->scale);
				break;
			case SCALE_ABSOLUTE:
				ow = oh = (int)c->scale;
				break;
			default:
				ow = (int)((float)in_w * c->scale);
				oh = (int)((float)in_h * c->scale);
				break;
			}
			if (c->max_scale > 0 && (ow > (int)((float)in_w * c->max_scale) ||
				oh > (int)((float)in_h * c->max_scale)))
			{
				ow = (int)((float)in_w * c->max_scale);
				oh = (int)((float)in_h * c->max_scale);
			}
			if (ow < 1) ow = 1;
			if (oh < 1) oh = 1;
			if (!size_output(p, ow, oh))
				return;
			GL(glBindFramebuffer)(GL_FRAMEBUFFER, p->fbo);
			GL(glViewport)(0, 0, ow, oh);
		}

		GL(glUseProgram)(p->prog);
		for (a = 0; a < MAX_ATTRIBS; a++)
			GL(glDisableVertexAttribArray)(a);
		if ((loc = GL(glGetAttribLocation)(p->prog, "VertexCoord")) >= 0)
		{
			GL(glEnableVertexAttribArray)((GLuint)loc);
			GL(glVertexAttribPointer)((GLuint)loc, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), NULL);
		}
		if ((loc = GL(glGetAttribLocation)(p->prog, "COLOR")) >= 0)
			GL(glVertexAttrib4f)((GLuint)loc, 1, 1, 1, 1);
		if ((loc = GL(glGetAttribLocation)(p->prog, "Color")) >= 0)
			GL(glVertexAttrib4f)((GLuint)loc, 1, 1, 1, 1);

		if ((loc = GL(glGetUniformLocation)(p->prog, "MVPMatrix")) >= 0)
			GL(glUniformMatrix4fv)(loc, 1, GL_FALSE, last ? mvp_window : mvp_fbo);
		if ((loc = GL(glGetUniformLocation)(p->prog, "FrameCount")) >= 0)
			GL(glUniform1i)(loc, frame_count);
		if ((loc = GL(glGetUniformLocation)(p->prog, "FrameDirection")) >= 0)
			GL(glUniform1i)(loc, 1);
		if ((loc = GL(glGetUniformLocation)(p->prog, "OutputSize")) >= 0)
			GL(glUniform2f)(loc, (GLfloat)ow, (GLfloat)oh);
		if ((loc = GL(glGetUniformLocation)(p->prog, "FinalViewportSize")) >= 0)
			GL(glUniform2f)(loc, (GLfloat)vw, (GLfloat)vh);

		/* this pass's input, then what came before it */
		set_filter(in_tex, c->filter_linear);
		unit = 0;
		bind_frame(p->prog, "", in_tex, in_w, in_h, &unit);
		bind_frame(p->prog, "Orig", orig, orig_w, orig_h, &unit);
		if (i > 0)
		{
			snprintf(name, sizeof(name), "PassPrev%d", i + 1);
			bind_frame(p->prog, name, orig, orig_w, orig_h, &unit);
		}
		for (j = 0; j < i; j++)
		{
			snprintf(name, sizeof(name), "Pass%d", j + 1);
			bind_frame(p->prog, name, passes[j].tex, passes[j].w, passes[j].h, &unit);
			snprintf(name, sizeof(name), "PassPrev%d", i - j);
			bind_frame(p->prog, name, passes[j].tex, passes[j].w, passes[j].h, &unit);
		}

		GL(glDrawArrays)(GL_TRIANGLE_STRIP, 0, 4);

		in_tex = p->tex;
		in_w = ow;
		in_h = oh;
	}
	GL(glColorMask)(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

	SDL_GL_SwapWindow(window);
	frame_count++;
}



/*
 * sysvid_gl_shutdown
 *
 * releases the GL objects, the context and the window. safe on a partial init.
 */
void sysvid_gl_shutdown(void)
{
	int i;

	if (context)
	{
		for (i = 0; i < NPASSES; i++)
		{
			if (passes[i].prog) GL(glDeleteProgram)(passes[i].prog);
			if (passes[i].tex) GL(glDeleteTextures)(1, &passes[i].tex);
			if (passes[i].fbo) GL(glDeleteFramebuffers)(1, &passes[i].fbo);
		}
		if (orig) GL(glDeleteTextures)(1, &orig);
		if (vbo) GL(glDeleteBuffers)(1, &vbo);
		if (vao) GL(glDeleteVertexArrays)(1, &vao);
		SDL_GL_DestroyContext(context);
	}
	if (window)
		SDL_DestroyWindow(window);
	SDL_memset(passes, 0, sizeof(passes));
	orig = vbo = vao = 0;
	context = NULL;
	window = NULL;
	SDL_GL_ResetAttributes();
}

#endif /* ENABLE_SHADERS */

/* eof */
