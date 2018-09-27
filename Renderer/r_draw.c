/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

// draw.c

#include "r_local.h"


// -----------------------------------------------------------------------------------------------------------------------------------------------------------------
// quadbatch
// the quadbatch stuff is only used for 2d drawing now...
// and we might even remove it from some of that...

typedef struct drawpolyvert_s {
	float position[2];
	float texcoord[2];
	union {
		DWORD color;
		byte rgba[4];
		float slice;
	};
} drawpolyvert_t;


// these should be sized such that MAX_DRAW_INDEXES = (MAX_DRAW_VERTS / 4) * 6
#define MAX_DRAW_VERTS		0x400
#define MAX_DRAW_INDEXES	0x600

#if (MAX_DRAW_VERTS / 4) * 6 != MAX_DRAW_INDEXES
#error (MAX_DRAW_VERTS / 4) * 6 != MAX_DRAW_INDEXES
#endif


static drawpolyvert_t *d_drawverts = NULL;
static int d_firstdrawvert = 0;
static int d_numdrawverts = 0;

static ID3D11Buffer *d3d_DrawVertexes = NULL;
static ID3D11Buffer *d3d_DrawIndexes = NULL;


void R_InitQuadBatch (void)
{
	D3D11_BUFFER_DESC vbDesc = {
		sizeof (drawpolyvert_t) * MAX_DRAW_VERTS,
		D3D11_USAGE_DYNAMIC,
		D3D11_BIND_VERTEX_BUFFER,
		D3D11_CPU_ACCESS_WRITE,
		0,
		0
	};

	D3D11_BUFFER_DESC ibDesc = {
		sizeof (unsigned short) * MAX_DRAW_INDEXES,
		D3D11_USAGE_DEFAULT,
		D3D11_BIND_INDEX_BUFFER,
		0,
		0,
		0
	};

	int i;
	unsigned short *ndx = ri.Load_AllocMemory (sizeof (unsigned short) * MAX_DRAW_INDEXES);
	D3D11_SUBRESOURCE_DATA srd = {ndx, 0, 0};

	for (i = 0; i < MAX_DRAW_VERTS; i += 4, ndx += 6)
	{
		ndx[0] = i + 0;
		ndx[1] = i + 1;
		ndx[2] = i + 2;

		ndx[3] = i + 0;
		ndx[4] = i + 2;
		ndx[5] = i + 3;
	}

	d3d_Device->lpVtbl->CreateBuffer (d3d_Device, &vbDesc, NULL, &d3d_DrawVertexes);
	d3d_Device->lpVtbl->CreateBuffer (d3d_Device, &ibDesc, &srd, &d3d_DrawIndexes);

	ri.Load_FreeMemory ();

	D_CacheObject ((ID3D11DeviceChild *) d3d_DrawIndexes, "d3d_DrawIndexes");
	D_CacheObject ((ID3D11DeviceChild *) d3d_DrawVertexes, "d3d_DrawVertexes");
}


void D_CheckQuadBatch (void)
{
	if (d_firstdrawvert + d_numdrawverts + 4 >= MAX_DRAW_VERTS)
	{
		// if we run out of buffer space for the next quad we flush the batch and begin a new one
		Draw_Flush ();
		d_firstdrawvert = 0;
	}

	if (!d_drawverts)
	{
		// first index is only reset to 0 if the buffer must wrap so this is valid to do
		D3D11_MAP mode = (d_firstdrawvert > 0) ? D3D11_MAP_WRITE_NO_OVERWRITE : D3D11_MAP_WRITE_DISCARD;
		D3D11_MAPPED_SUBRESOURCE msr;

		if (FAILED (d3d_Context->lpVtbl->Map (d3d_Context, (ID3D11Resource *) d3d_DrawVertexes, 0, mode, 0, &msr)))
			return;
		else d_drawverts = (drawpolyvert_t *) msr.pData + d_firstdrawvert;
	}
}


// -----------------------------------------------------------------------------------------------------------------------------------------------------------------


#define STAT_MINUS		10	// num frame for '-' stats digit

static image_t	*draw_chars;
static image_t	*sb_nums[2];

static int d3d_DrawTexturedShader;
static int d3d_DrawCinematicShader;
static int d3d_DrawColouredShader;
static int d3d_DrawTexArrayShader;


static ID3D11Buffer *d3d_DrawConstants = NULL;
static ID3D11Buffer *d3d_CineConstants = NULL;


typedef struct drawconstants_s {
	QMATRIX OrthoMatrix;
	float gamma;
	float contrast;
	float junk[2];
} drawconstants_t;


/*
===============
Draw_InitLocal
===============
*/
void Draw_InitLocal (void)
{
	D3D11_BUFFER_DESC cbDrawDesc = {
		sizeof (drawconstants_t),
		D3D11_USAGE_DEFAULT,
		D3D11_BIND_CONSTANT_BUFFER,
		0,
		0,
		0
	};

	D3D11_BUFFER_DESC cbCineDesc = {
		sizeof (QMATRIX),
		D3D11_USAGE_DEFAULT,
		D3D11_BIND_CONSTANT_BUFFER,
		0,
		0,
		0
	};

	D3D11_INPUT_ELEMENT_DESC layout_standard[] = {
		VDECL ("POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0),
		VDECL ("TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0),
		VDECL ("COLOUR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 0)
	};

	D3D11_INPUT_ELEMENT_DESC layout_texarray[] = {
		VDECL ("POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0),
		VDECL ("TEXCOORD", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0)
	};

	// buffers
	d3d_Device->lpVtbl->CreateBuffer (d3d_Device, &cbDrawDesc, NULL, &d3d_DrawConstants);
	D_RegisterConstantBuffer (d3d_DrawConstants, 0);

	d3d_Device->lpVtbl->CreateBuffer (d3d_Device, &cbCineDesc, NULL, &d3d_CineConstants);
	D_RegisterConstantBuffer (d3d_CineConstants, 7);

	// shaders
	d3d_DrawTexturedShader = D_CreateShaderBundle (IDR_DRAWSHADER, "DrawTexturedVS", NULL, "DrawTexturedPS", DEFINE_LAYOUT (layout_standard));
	d3d_DrawColouredShader = D_CreateShaderBundle (IDR_DRAWSHADER, "DrawColouredVS", NULL, "DrawColouredPS", DEFINE_LAYOUT (layout_standard));
	d3d_DrawTexArrayShader = D_CreateShaderBundle (IDR_DRAWSHADER, "DrawTexArrayVS", NULL, "DrawTexArrayPS", DEFINE_LAYOUT (layout_texarray));

	// non-quadbatch shaders
	d3d_DrawCinematicShader = D_CreateShaderBundle (IDR_DRAWSHADER, "DrawCinematicVS", NULL, "DrawCinematicPS", NULL, 0);

	// load console characters
	draw_chars = GL_FindImage ("pics/conchars.pcx", it_charset);
	sb_nums[0] = GL_LoadTexArray ("num");
	sb_nums[1] = GL_LoadTexArray ("anum");
}


void D_UpdateDrawConstants (void)
{
	drawconstants_t consts;

	R_MatrixIdentity (&consts.OrthoMatrix);
	R_MatrixOrtho (&consts.OrthoMatrix, 0, vid.conwidth, vid.conheight, 0, -1, 1);

	consts.gamma = vid_gamma->value;
	consts.contrast = 1.0f;

	d3d_Context->lpVtbl->UpdateSubresource (d3d_Context, (ID3D11Resource *) d3d_DrawConstants, 0, NULL, &consts, 0, 0);
}


void Draw_Flush (void)
{
	if (d_drawverts)
	{
		d3d_Context->lpVtbl->Unmap (d3d_Context, (ID3D11Resource *) d3d_DrawVertexes, 0);
		d_drawverts = NULL;
	}

	if (d_numdrawverts)
	{
		D_BindVertexBuffer (0, d3d_DrawVertexes, sizeof (drawpolyvert_t), 0);
		D_BindIndexBuffer (d3d_DrawIndexes, DXGI_FORMAT_R16_UINT);

		d3d_Context->lpVtbl->DrawIndexed (d3d_Context, (d_numdrawverts >> 2) * 6, 0, d_firstdrawvert);

		d_firstdrawvert += d_numdrawverts;
		d_numdrawverts = 0;
	}
}


void Draw_TexturedVertex (drawpolyvert_t *vert, float x, float y, DWORD color, float s, float t)
{
	vert->position[0] = x;
	vert->position[1] = y;

	vert->texcoord[0] = s;
	vert->texcoord[1] = t;

	vert->color = color;
}


void Draw_ColouredVertex (drawpolyvert_t *vert, float x, float y, DWORD color)
{
	vert->position[0] = x;
	vert->position[1] = y;

	vert->color = color;
}


void Draw_CharacterVertex (drawpolyvert_t *vert, float x, float y, float s, float t, float slice)
{
	vert->position[0] = x;
	vert->position[1] = y;

	vert->texcoord[0] = s;
	vert->texcoord[1] = t;

	vert->slice = slice;
}


void Draw_TexturedQuad (image_t *image, int x, int y, int w, int h, unsigned color)
{
	R_BindTexture (image->SRV);

	D_BindShaderBundle (d3d_DrawTexturedShader);
	D_SetRenderStates (d3d_BSAlphaPreMult, d3d_DSNoDepth, d3d_RSNoCull);

	D_CheckQuadBatch ();

	Draw_TexturedVertex (&d_drawverts[d_numdrawverts++], x, y, color, 0, 0);
	Draw_TexturedVertex (&d_drawverts[d_numdrawverts++], x + w, y, color, 1, 0);
	Draw_TexturedVertex (&d_drawverts[d_numdrawverts++], x + w, y + h, color, 1, 1);
	Draw_TexturedVertex (&d_drawverts[d_numdrawverts++], x, y + h, color, 0, 1);

	Draw_Flush ();
}


void Draw_ColouredQuad (int x, int y, int w, int h, unsigned colour)
{
	D_BindShaderBundle (d3d_DrawColouredShader);
	D_SetRenderStates (d3d_BSAlphaBlend, d3d_DSNoDepth, d3d_RSNoCull);

	D_CheckQuadBatch ();

	Draw_ColouredVertex (&d_drawverts[d_numdrawverts++], x, y, colour);
	Draw_ColouredVertex (&d_drawverts[d_numdrawverts++], x + w, y, colour);
	Draw_ColouredVertex (&d_drawverts[d_numdrawverts++], x + w, y + h, colour);
	Draw_ColouredVertex (&d_drawverts[d_numdrawverts++], x, y + h, colour);

	Draw_Flush ();
}


void Draw_CharacterQuad (int x, int y, int w, int h, int slice)
{
	// check for overflow
	D_CheckQuadBatch ();

	// and draw it
	Draw_CharacterVertex (&d_drawverts[d_numdrawverts++], x, y, 0, 0, slice);
	Draw_CharacterVertex (&d_drawverts[d_numdrawverts++], x + w, y, 1, 0, slice);
	Draw_CharacterVertex (&d_drawverts[d_numdrawverts++], x + w, y + h, 1, 1, slice);
	Draw_CharacterVertex (&d_drawverts[d_numdrawverts++], x, y + h, 0, 1, slice);
}


void Draw_Field (int x, int y, int color, int width, int value)
{
	char	num[16], *ptr;
	int		l;
	int		frame;

	if (width < 1)
		return;

	// draw number string
	if (width > 5)
		width = 5;

	Com_sprintf (num, sizeof (num), "%i", value);
	l = strlen (num);

	if (l > width)
		l = width;

	x += 2 + sb_nums[color]->width * (width - l);
	ptr = num;

	GL_BindTexArray (sb_nums[color]->SRV);

	D_BindShaderBundle (d3d_DrawTexArrayShader);
	D_SetRenderStates (d3d_BSAlphaPreMult, d3d_DSNoDepth, d3d_RSNoCull);

	while (*ptr && l)
	{
		if (*ptr == '-')
			frame = STAT_MINUS;
		else frame = *ptr - '0';

		Draw_CharacterQuad (x, y, sb_nums[color]->width, sb_nums[color]->height, frame);

		x += sb_nums[color]->width;
		ptr++;
		l--;
	}

	Draw_Flush ();
}


/*
================
Draw_Char

Draws one 8*8 graphics character with 0 being transparent.
It can be clipped to the top of the screen to allow the console to be
smoothly scrolled off.
================
*/
void Draw_Char (int x, int y, int num)
{
	// totally off screen
	if (y <= -8) return;

	// space
	if ((num & 127) == 32) return;

	// these are done for each char but they only trigger state changes for the first
	GL_BindTexArray (draw_chars->SRV);

	D_BindShaderBundle (d3d_DrawTexArrayShader);
	D_SetRenderStates (d3d_BSAlphaPreMult, d3d_DSNoDepth, d3d_RSNoCull);

	Draw_CharacterQuad (x, y, 8, 8, num & 255);
}


/*
=============
Draw_FindPic
=============
*/
image_t	*Draw_FindPic (char *name)
{
	if (name[0] != '/' && name[0] != '\\')
		return GL_FindImage (va ("pics/%s.pcx", name), it_pic);
	else return GL_FindImage (name + 1, it_pic);
}

/*
=============
Draw_GetPicSize
=============
*/
void Draw_GetPicSize (int *w, int *h, char *pic)
{
	image_t *gl = Draw_FindPic (pic);

	if (!gl)
	{
		*w = *h = -1;
		return;
	}

	*w = gl->width;
	*h = gl->height;
}


/*
=============
Draw_Pic
=============
*/
void Draw_Pic (int x, int y, char *pic)
{
	image_t *gl = Draw_FindPic (pic);

	if (!gl)
	{
		ri.Con_Printf (PRINT_ALL, "Can't find pic: %s\n", pic);
		return;
	}

	Draw_TexturedQuad (gl, x, y, gl->width, gl->height, 0xffffffff);
}


void Draw_ConsoleBackground (int x, int y, int w, int h, char *pic, int alpha)
{
	image_t *gl = Draw_FindPic (pic);

	if (!gl)
	{
		ri.Con_Printf (PRINT_ALL, "Can't find pic: %s\n", pic);
		return;
	}

	if (alpha >= 255)
		Draw_TexturedQuad (gl, x, y, w, h, 0xffffffff);
	else if (alpha > 0)
		Draw_TexturedQuad (gl, x, y, w, h, (alpha << 24) | 0xffffff);
}


/*
=============
Draw_Fill

Fills a box of pixels with a single color
=============
*/
void Draw_Fill (int x, int y, int w, int h, int c)
{
	Draw_ColouredQuad (x, y, w, h, d_8to24table[c]);
}

//=============================================================================

/*
================
Draw_FadeScreen

================
*/
void Draw_FadeScreen (void)
{
	Draw_ColouredQuad (0, 0, vid.conwidth, vid.conheight, 0xcc000000);
}


//====================================================================


/*
=============
Draw_StretchRaw
=============
*/
ID3D11Texture2D *r_RawTexture = NULL;
ID3D11ShaderResourceView *r_RawSRV = NULL;
D3D11_TEXTURE2D_DESC r_RawDesc;

void Draw_ShutdownRawImage (void)
{
	SAFE_RELEASE (r_RawTexture);
	SAFE_RELEASE (r_RawSRV);
	memset (&r_RawDesc, 0, sizeof (r_RawDesc));
}


unsigned r_rawpalette[256];

unsigned *GL_Image8To32 (byte *data, int width, int height, unsigned *palette);
void R_TexSubImage32 (ID3D11Texture2D *tex, int level, int x, int y, int w, int h, unsigned *data);
void R_TexSubImage8 (ID3D11Texture2D *tex, int level, int x, int y, int w, int h, byte *data, unsigned *palette);

void R_SetCinematicPalette (const unsigned char *palette)
{
	if (palette)
		Image_QuakePalFromPCXPal (r_rawpalette, palette);
	else memcpy (r_rawpalette, d_8to24table, sizeof (d_8to24table));

	// refresh the texture if the palette changes
	Draw_ShutdownRawImage ();
}


void Draw_StretchRaw (int cols, int rows, byte *data, int frame)
{
	// we only need to refresh the texture if the frame changes
	static int r_rawframe = -1;

	// matrix transform for positioning the cinematic correctly
	// sampler state should be set to clamp-to-border with a border color of black
	QMATRIX cineMatrix;
	float strans, ttrans;

	// ensure the correct viewport is set
	D3D11_VIEWPORT vp = {0, 0, vid.width, vid.height, 0, 0};
	d3d_Context->lpVtbl->RSSetViewports (d3d_Context, 1, &vp);

	// if the dimensions change the texture needs to be recreated
	if (r_RawDesc.Width != cols || r_RawDesc.Height != rows)
		Draw_ShutdownRawImage ();

	if (!r_RawTexture || !r_RawSRV)
	{
		// ensure in case we got a partial creation
		Draw_ShutdownRawImage ();

		// describe the texture
		R_DescribeTexture (&r_RawDesc, cols, rows, 1, TEX_RGBA8);

		// failure is not an option...
		if (FAILED (d3d_Device->lpVtbl->CreateTexture2D (d3d_Device, &r_RawDesc, NULL, &r_RawTexture))) ri.Sys_Error (ERR_FATAL, "CreateTexture2D failed");
		if (FAILED (d3d_Device->lpVtbl->CreateShaderResourceView (d3d_Device, (ID3D11Resource *) r_RawTexture, NULL, &r_RawSRV))) ri.Sys_Error (ERR_FATAL, "CreateShaderResourceView failed");

		// load the image
		r_rawframe = -1;
	}

	if (r_rawframe != frame)
	{
		// reload data if required
		R_TexSubImage8 (r_RawTexture, 0, 0, 0, cols, rows, data, r_rawpalette);
		r_rawframe = frame;
	}

	// free any memory we may have used for loading it
	ri.Load_FreeMemory ();

	// derive the texture matrix for the cinematic pic
	if (vid.width > vid.height)
	{
		strans = 0.5f * ((float) rows / (float) cols) * ((float) vid.width / (float) vid.height);
		ttrans = -0.5f;
	}
	else
	{
		strans = 0.5f;
		ttrans = -0.5f * ((float) cols / (float) rows) * ((float) vid.height / (float) vid.width);
	}

	// load it on
	R_MatrixLoadf (&cineMatrix, strans, 0, 0, 0, 0, ttrans, 0, 0, 0, 0, 1, 0, 0.5f, 0.5f, 0, 1);

	// and upload it to the GPU
	d3d_Context->lpVtbl->UpdateSubresource (d3d_Context, (ID3D11Resource *) d3d_CineConstants, 0, NULL, &cineMatrix, 0, 0);

	R_BindTexture (r_RawSRV);

	D_BindShaderBundle (d3d_DrawCinematicShader);
	D_SetRenderStates (d3d_BSNone, d3d_DSNoDepth, d3d_RSNoCull);

	d3d_Context->lpVtbl->Draw (d3d_Context, 3, 0);
}

