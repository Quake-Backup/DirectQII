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
// r_main.c
#include "r_local.h"

void R_DrawSpriteModel (entity_t *e);
void R_DrawNullModel (entity_t *e);
void R_DrawParticles (void);
void R_DrawBeam (entity_t *e);
void R_SetupSky (QMATRIX *SkyMatrix);
void R_SetLightLevel (void);


typedef struct mainconstants_s {
	QMATRIX mvpMatrix;
	float viewOrigin[4]; // padded for cbuffer packing
	float viewForward[4]; // padded for cbuffer packing
	float viewRight[4]; // padded for cbuffer packing
	float viewUp[4]; // padded for cbuffer packing
	float vBlend[4];
	float WarpTime[2];
	float TexScroll;
	float turbTime;
	QMATRIX skyMatrix;
} mainconstants_t;

typedef struct entityconstants_s {
	QMATRIX localMatrix;
	float shadecolor[3];
	float alphaVal;
} entityconstants_t;


ID3D11Buffer *d3d_MainConstants = NULL;
ID3D11Buffer *d3d_EntityConstants = NULL;

int d3d_PolyblendShader = 0;

void R_InitMain (void)
{
	D3D11_BUFFER_DESC cbMainDesc = {
		sizeof (mainconstants_t),
		D3D11_USAGE_DEFAULT,
		D3D11_BIND_CONSTANT_BUFFER,
		0,
		0,
		0
	};

	D3D11_BUFFER_DESC cbEntityDesc = {
		sizeof (entityconstants_t),
		D3D11_USAGE_DEFAULT,
		D3D11_BIND_CONSTANT_BUFFER,
		0,
		0,
		0
	};

	d3d_Device->lpVtbl->CreateBuffer (d3d_Device, &cbMainDesc, NULL, &d3d_MainConstants);
	d3d_Device->lpVtbl->CreateBuffer (d3d_Device, &cbEntityDesc, NULL, &d3d_EntityConstants);

	D_RegisterConstantBuffer (d3d_MainConstants, 1);
	D_RegisterConstantBuffer (d3d_EntityConstants, 2);

	d3d_PolyblendShader = D_CreateShaderBundleForQuadBatch (IDR_DRAWSHADER, "DrawPolyblendVS", "DrawPolyblendPS", batch_standard);
}


viddef_t	vid;

refimport_t	ri;

model_t		*r_worldmodel;

glconfig_t gl_config;
glstate_t  gl_state;

image_t		*r_notexture;		// use for bad textures
image_t		*r_blacktexture;	// use for bad textures
image_t		*r_greytexture;		// use for bad textures
image_t		*r_whitetexture;	// use for bad textures

cplane_t	frustum[4];

int			r_visframecount;	// bumped when going to a new PVS
int			r_framecount;		// used for dlight push checking

int			c_brush_polys, c_alias_polys;

float		v_blend[4];			// final blending color

// world transforms
QMATRIX	r_view_matrix;
QMATRIX	r_proj_matrix;
QMATRIX	r_mvp_matrix;

// view origin
vec3_t	vup;
vec3_t	vpn;
vec3_t	vright;
vec3_t	r_origin;

// screen size info
refdef_t	r_newrefdef;

int		r_viewcluster, r_viewcluster2, r_oldviewcluster, r_oldviewcluster2;

cvar_t	*r_lightmap;
cvar_t	*r_fullbright;
cvar_t	*r_beamdetail;
cvar_t	*r_drawentities;
cvar_t	*r_drawworld;
cvar_t	*r_novis;
cvar_t	*r_lefthand;

cvar_t	*r_lightlevel;	// FIXME: This is a HACK to get the client's light level

cvar_t	*gl_mode;
cvar_t	*gl_finish;
cvar_t	*gl_clear;
cvar_t	*gl_polyblend;
cvar_t	*gl_lockpvs;

cvar_t	*vid_fullscreen;
cvar_t	*vid_gamma;
cvar_t	*vid_ref;

cvar_t	*r_fov;


void R_UpdateEntityConstants (QMATRIX *localMatrix, float *color, float alphaval, int rflags)
{
	entityconstants_t consts;

	// we need to retain the local matrix for lighting so transform it into the cBuffer directly
	if (((rflags & RF_WEAPONMODEL) && r_lefthand->value) || (rflags & RF_DEPTHHACK))
	{
		QMATRIX flipmatrix;

		R_MatrixIdentity (&flipmatrix);

		// scale the matrix to flip from right-handed to left-handed
		if ((rflags & RF_WEAPONMODEL) && r_lefthand->value) R_MatrixScale (&flipmatrix, -1, 1, 1);

		// scale the matrix to provide the depthrange hack (so that we don't need to set a new VP with the modified range)
		if (rflags & RF_DEPTHHACK) R_MatrixScale (&flipmatrix, 1.0f, 1.0f, 0.3f);

		// multiply by MVP for the final matrix
		if (r_fov->value > 90)
		{
			// to do - using a separate FOV for the gun if > 90
		}
		else R_MatrixMultiply (&flipmatrix, &r_mvp_matrix, &flipmatrix);

		R_MatrixMultiply (&consts.localMatrix, localMatrix, &flipmatrix);
	}
	else R_MatrixMultiply (&consts.localMatrix, localMatrix, &r_mvp_matrix);

	// the color param can be NULL indicating white
	if (color)
		Vector3Copy (consts.shadecolor, color);
	else Vector3Set (consts.shadecolor, 1, 1, 1);

	// other stuff
	consts.alphaVal = alphaval;

	// and update to the cbuffer
	d3d_Context->lpVtbl->UpdateSubresource (d3d_Context, (ID3D11Resource *) d3d_EntityConstants, 0, NULL, &consts, 0, 0);
}


/*
=================
R_CullBox

Returns true if the box is completely outside the frustom
=================
*/
qboolean R_CullSphere (float *center, float radius)
{
	if (Vector3Dot (center, frustum[0].normal) - frustum[0].dist <= -radius) return true;
	if (Vector3Dot (center, frustum[1].normal) - frustum[1].dist <= -radius) return true;
	if (Vector3Dot (center, frustum[2].normal) - frustum[2].dist <= -radius) return true;
	if (Vector3Dot (center, frustum[3].normal) - frustum[3].dist <= -radius) return true;

	return false;
}


qboolean R_CullBox (vec3_t emins, vec3_t emaxs)
{
	int		i;

	for (i = 0; i < 4; i++)
	{
		cplane_t *p = &frustum[i];

		switch (p->signbits)
		{
		default:
		case 0: if (p->normal[0] * emaxs[0] + p->normal[1] * emaxs[1] + p->normal[2] * emaxs[2] < p->dist) return true; break;
		case 1: if (p->normal[0] * emins[0] + p->normal[1] * emaxs[1] + p->normal[2] * emaxs[2] < p->dist) return true; break;
		case 2: if (p->normal[0] * emaxs[0] + p->normal[1] * emins[1] + p->normal[2] * emaxs[2] < p->dist) return true; break;
		case 3: if (p->normal[0] * emins[0] + p->normal[1] * emins[1] + p->normal[2] * emaxs[2] < p->dist) return true; break;
		case 4: if (p->normal[0] * emaxs[0] + p->normal[1] * emaxs[1] + p->normal[2] * emins[2] < p->dist) return true; break;
		case 5: if (p->normal[0] * emins[0] + p->normal[1] * emaxs[1] + p->normal[2] * emins[2] < p->dist) return true; break;
		case 6: if (p->normal[0] * emaxs[0] + p->normal[1] * emins[1] + p->normal[2] * emins[2] < p->dist) return true; break;
		case 7: if (p->normal[0] * emins[0] + p->normal[1] * emins[1] + p->normal[2] * emins[2] < p->dist) return true; break;
		}
	}

	return false;
}


qboolean R_CullBoxClipflags (vec3_t mins, vec3_t maxs, int clipflags)
{
	// only used by the surf code now
	int i;
	int side;

	for (i = 0; i < 4; i++)
	{
		// don't need to clip against it
		if (!(clipflags & (1 << i))) continue;

		// the frustum planes are *never* axial so there's no point in doing the "fast" pre-test
		side = BoxOnPlaneSide (mins, maxs, &frustum[i]);

		if (side == 1) clipflags &= ~(1 << i);
		if (side == 2) return true;
	}

	// onscreen
	return false;
}


//==================================================================================


void R_DrawEntitiesOnList (qboolean trans)
{
	int		i;

	if (!r_drawentities->value)
		return;

	for (i = 0; i < r_newrefdef.num_entities; i++)
	{
		entity_t *e = &r_newrefdef.entities[i];

		if (!trans && (e->flags & RF_TRANSLUCENT)) continue;
		if (trans && !(e->flags & RF_TRANSLUCENT)) continue;

		if (e->flags & RF_BEAM)
		{
			R_DrawBeam (e);
		}
		else
		{
			if (!e->model)
			{
				R_DrawNullModel (e);
				continue;
			}

			switch (e->model->type)
			{
			case mod_alias:
				R_DrawAliasModel (e);
				break;

			case mod_brush:
				R_DrawBrushModel (e);
				break;

			case mod_sprite:
				R_DrawSpriteModel (e);
				break;

			default:
				ri.Sys_Error (ERR_DROP, "Bad modeltype");
				break;
			}
		}
	}
}


/*
============
R_PolyBlend
============
*/
void R_PolyBlend (void)
{
	if (v_blend[3] > 0)
	{
		D_SetRenderStates (d3d_BSAlphaBlend, d3d_DSDepthNoWrite, d3d_RSNoCull);
		D_BindShaderBundle (d3d_PolyblendShader);

		D_CheckQuadBatch ();

		D_QuadVertexPosition2f (-1, -1);
		D_QuadVertexPosition2f ( 1, -1);
		D_QuadVertexPosition2f ( 1,  1);
		D_QuadVertexPosition2f (-1,  1);

		D_EndQuadBatch ();
	}
}


//=======================================================================


/*
===============
R_SetupFrame
===============
*/
void R_SetupFrame (void)
{
	int i;
	mleaf_t	*leaf;

	r_framecount++;

	// build the transformation matrix for the given view angles
	VectorCopy (r_newrefdef.vieworg, r_origin);
	AngleVectors (r_newrefdef.viewangles, vpn, vright, vup);

	// current viewcluster
	if (!(r_newrefdef.rdflags & RDF_NOWORLDMODEL))
	{
		r_oldviewcluster = r_viewcluster;
		r_oldviewcluster2 = r_viewcluster2;
		leaf = Mod_PointInLeaf (r_origin, r_worldmodel);
		r_viewcluster = r_viewcluster2 = leaf->cluster;

		// check above and below so crossing solid water doesn't draw wrong
		if (!leaf->contents)
		{
			// look down a bit
			vec3_t	temp;

			VectorCopy (r_origin, temp);
			temp[2] -= 16;
			leaf = Mod_PointInLeaf (temp, r_worldmodel);
			if (!(leaf->contents & CONTENTS_SOLID) && (leaf->cluster != r_viewcluster2))
				r_viewcluster2 = leaf->cluster;
		}
		else
		{
			// look up a bit
			vec3_t	temp;

			VectorCopy (r_origin, temp);
			temp[2] += 16;
			leaf = Mod_PointInLeaf (temp, r_worldmodel);
			if (!(leaf->contents & CONTENTS_SOLID) && (leaf->cluster != r_viewcluster2))
				r_viewcluster2 = leaf->cluster;
		}
	}

	for (i = 0; i < 4; i++)
		v_blend[i] = r_newrefdef.blend[i];

	// scale for value of gl_polyblend
	v_blend[3] *= gl_polyblend->value;

	c_brush_polys = 0;
	c_alias_polys = 0;

	// clear out the portion of the screen that the NOWORLDMODEL defines
	if (r_newrefdef.rdflags & RDF_NOWORLDMODEL)
	{
		// we can't clear subrects in D3D11 so just clear the entire thing
		d3d_Context->lpVtbl->ClearDepthStencilView (d3d_Context, d3d_DepthBuffer, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1, 1);
	}
}


void R_ExtractFrustum (cplane_t *f, QMATRIX *m)
{
	int i;

	// extract the frustum from the MVP matrix
	f[0].normal[0] = m->m4x4[0][3] - m->m4x4[0][0];
	f[0].normal[1] = m->m4x4[1][3] - m->m4x4[1][0];
	f[0].normal[2] = m->m4x4[2][3] - m->m4x4[2][0];

	f[1].normal[0] = m->m4x4[0][3] + m->m4x4[0][0];
	f[1].normal[1] = m->m4x4[1][3] + m->m4x4[1][0];
	f[1].normal[2] = m->m4x4[2][3] + m->m4x4[2][0];

	f[2].normal[0] = m->m4x4[0][3] + m->m4x4[0][1];
	f[2].normal[1] = m->m4x4[1][3] + m->m4x4[1][1];
	f[2].normal[2] = m->m4x4[2][3] + m->m4x4[2][1];

	f[3].normal[0] = m->m4x4[0][3] - m->m4x4[0][1];
	f[3].normal[1] = m->m4x4[1][3] - m->m4x4[1][1];
	f[3].normal[2] = m->m4x4[2][3] - m->m4x4[2][1];

	for (i = 0; i < 4; i++)
	{
		Vector3Normalize (f[i].normal);
		frustum[i].type = PLANE_ANYZ;
		f[i].dist = Vector3Dot (r_newrefdef.vieworg, f[i].normal);
		f[i].signbits = Mod_SignbitsForPlane (&f[i]);
	}
}


float R_GetFarClip (void)
{
	// this provides the maximum far clip per quake protocol limits
	float mins[3] = {-4096, -4096, -4096};
	float maxs[3] = {4096, 4096, 4096};
	return Vector3Dist (mins, maxs);
}


/*
=============
R_SetupGL
=============
*/
void R_SetupGL (void)
{
	mainconstants_t consts;
	double junk[2] = {0, 0};

	// set up the viewport that we'll use for the entire refresh
	D3D11_VIEWPORT vp = {r_newrefdef.x, r_newrefdef.y, r_newrefdef.width, r_newrefdef.height, 0, 1};
	d3d_Context->lpVtbl->RSSetViewports (d3d_Context, 1, &vp);

	// the projection matrix may be only updated when the refdef changes but we do it every frame so that we can do waterwarp
	R_MatrixIdentity (&r_proj_matrix);
	R_MatrixFrustum (&r_proj_matrix, r_newrefdef.fov_x, r_newrefdef.fov_y, 4.0f, R_GetFarClip ());

	// modelview is updated every frame; done in reverse so that we can use the new sexy rotation on it
	R_MatrixIdentity (&r_view_matrix);
	R_MatrixCamera (&r_view_matrix, r_newrefdef.vieworg, r_newrefdef.viewangles);

	// compute the global MVP
	R_MatrixMultiply (&r_mvp_matrix, &r_view_matrix, &r_proj_matrix);

	// and extract the frustum from it
	R_ExtractFrustum (frustum, &r_mvp_matrix);

	// take these from the view matrix
	Vector3Set (vpn,   -r_view_matrix.m4x4[0][2], -r_view_matrix.m4x4[1][2], -r_view_matrix.m4x4[2][2]);
	Vector3Set (vup,    r_view_matrix.m4x4[0][1],  r_view_matrix.m4x4[1][1],  r_view_matrix.m4x4[2][1]);
	Vector3Set (vright, r_view_matrix.m4x4[0][0],  r_view_matrix.m4x4[1][0],  r_view_matrix.m4x4[2][0]);

	// setup the shader constants that are going to remain unchanged for the frame; time-based animations, etc
	R_MatrixLoad (&consts.mvpMatrix, &r_mvp_matrix);

	Vector3Copy (consts.viewOrigin,	 r_newrefdef.vieworg);
	Vector3Copy (consts.viewForward, vpn);
	Vector3Copy (consts.viewRight,	 vright);
	Vector3Copy (consts.viewUp,		 vup);

	Vector4Copy (consts.vBlend, v_blend);

	consts.WarpTime[0] = modf (r_newrefdef.time * 0.09f, &junk[0]);
	consts.WarpTime[1] = modf (r_newrefdef.time * 0.11f, &junk[1]);

	consts.TexScroll = modf ((double) r_newrefdef.time / 40.0, &junk[0]) * -64.0f;

	// cycle in increments of 2 * M_PI so that the sine warp will wrap correctly
	consts.turbTime = modf ((double) r_newrefdef.time / (M_PI * 2.0), junk) * (M_PI * 2.0);

	// set up sky for drawing (also binds the sky texture)
	R_SetupSky (&consts.skyMatrix);

	// and update to the cbuffer
	d3d_Context->lpVtbl->UpdateSubresource (d3d_Context, (ID3D11Resource *) d3d_MainConstants, 0, NULL, &consts, 0, 0);
}


/*
=============
R_Clear
=============
*/
void R_Clear (ID3D11RenderTargetView *RTV, ID3D11DepthStencilView *DSV)
{
	mleaf_t *leaf = Mod_PointInLeaf (r_origin, r_worldmodel);

	if (leaf->contents & CONTENTS_SOLID)
	{
		// if the view is inside solid it's probably because we're noclipping so we need to clear so as to not get HOM effects
		float clear[4] = {0.1f, 0.1f, 0.1f, 0.0f};
		d3d_Context->lpVtbl->ClearRenderTargetView (d3d_Context, RTV, clear);
	}
	else if (gl_clear->value)
	{
		float clear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
		d3d_Context->lpVtbl->ClearRenderTargetView (d3d_Context, RTV, clear);
	}

	// standard depth clear
	d3d_Context->lpVtbl->ClearDepthStencilView (d3d_Context, DSV, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1, 1);
}


void R_SyncPipeline (void)
{
	// forces the pipeline to sync by issuing a query then waiting for it to complete
	ID3D11Query *FinishEvent = NULL;
	D3D11_QUERY_DESC Desc = {D3D11_QUERY_EVENT, 0};

	if (SUCCEEDED (d3d_Device->lpVtbl->CreateQuery (d3d_Device, &Desc, &FinishEvent)))
	{
		d3d_Context->lpVtbl->End (d3d_Context, (ID3D11Asynchronous *) FinishEvent);
		while (d3d_Context->lpVtbl->GetData (d3d_Context, (ID3D11Asynchronous *) FinishEvent, NULL, 0, 0) == S_FALSE);
		SAFE_RELEASE (FinishEvent);
	}
}


void R_RenderScene (void)
{
	R_DrawWorld ();

	R_DrawEntitiesOnList (false);

	R_DrawEntitiesOnList (true);

	R_DrawParticles ();

	R_DrawAlphaSurfaces ();
}


void R_Set2DMode (void)
{
	// this just changes the viewport nowadays
	D3D11_VIEWPORT vp = {0, 0, vid.width, vid.height, 0, 0};
	d3d_Context->lpVtbl->RSSetViewports (d3d_Context, 1, &vp);
}


/*
====================
R_RenderFrame

====================
*/
void R_RenderFrame (refdef_t *fd)
{
	r_newrefdef = *fd;

	if (!r_worldmodel && !(r_newrefdef.rdflags & RDF_NOWORLDMODEL))
		ri.Sys_Error (ERR_DROP, "R_RenderFrame: NULL worldmodel");

	R_BindLightmaps ();

	if (gl_finish->value)
		R_SyncPipeline ();

	R_SetupFrame ();

	R_SetupGL ();

	R_MarkLeaves ();	// done here so we know if we're in water

	if (D_BeginWaterWarp ())
	{
		R_RenderScene ();
		D_DoWaterWarp ();
	}
	else
	{
		R_RenderScene ();
		R_PolyBlend ();
	}

	R_SetLightLevel ();

	// go back to 2d mode
	R_Set2DMode ();
}


/*
===============
R_BeginFrame
===============
*/
void R_BeginFrame (void)
{
	// change modes if necessary
	if (gl_mode->modified || vid_fullscreen->modified)
	{
		// FIXME: only restart if CDS is required
		cvar_t	*ref = ri.Cvar_Get ("vid_ref", "gl", 0);
		ref->modified = true;
	}

	GLimp_BeginFrame ();
}

