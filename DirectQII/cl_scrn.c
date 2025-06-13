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
// cl_scrn.c -- master for refresh, status bar, console, chat, notify, etc

/*

full screen console
put up loading plaque
blanked background with loading plaque
blanked background with menu
cinematics
full screen image for quit and victory

end of unit intermissions

*/

#include "client.h"

float		scr_con_current;	// aproaches scr_conlines at scr_conspeed
float		scr_conlines;		// 0.0 to 1.0 lines of console to display

qboolean	scr_initialized;		// ready to draw

int			scr_draw_loading;


cvar_t		*scr_conspeed;
cvar_t		*scr_centertime;
cvar_t		*scr_showturtle;
cvar_t		*scr_showpause;
cvar_t		*scr_printspeed;

cvar_t		*scr_netgraph;
cvar_t		*scr_timegraph;
cvar_t		*scr_debuggraph;
cvar_t		*scr_graphheight;
cvar_t		*scr_graphscale;
cvar_t		*scr_graphshift;

char		crosshair_pic[MAX_QPATH];
int			crosshair_width, crosshair_height;

void SCR_TimeRefresh_f (void);
void SCR_Loading_f (void);
void SCR_DrawCrosshair (void);
void SCR_ExecuteLayoutString (char *s);


int Com_CursorTime (void)
{
	// returns 0 or 1
	return ((cls.realtime >> 8) & 1);
}

/*
===============================================================================

BAR GRAPHS

===============================================================================
*/

/*
==============
CL_AddNetgraph

A new packet was just parsed
==============
*/
void CL_AddNetgraph (void)
{
	int		i;
	int		in;
	int		ping;

	// if using the debuggraph for something else, don't
	// add the net lines
	if (scr_debuggraph->value || scr_timegraph->value)
		return;

	for (i = 0; i < cls.netchan.dropped; i++)
		SCR_DebugGraph (30, 0x40);

	for (i = 0; i < cl.surpressCount; i++)
		SCR_DebugGraph (30, 0xdf);

	// see what the latency was on this packet
	in = cls.netchan.incoming_acknowledged & (CMD_BACKUP - 1);
	ping = cls.realtime - cl.cmd_time[in];
	ping /= 30;
	if (ping > 30)
		ping = 30;
	SCR_DebugGraph (ping, 0xd0);
}


typedef struct graphsamp_s {
	float	value;
	int		color;
} graphsamp_t;

static	int			current;
static	graphsamp_t	values[1024];

/*
==============
SCR_DebugGraph
==============
*/
void SCR_DebugGraph (float value, int color)
{
	values[current & 1023].value = value;
	values[current & 1023].color = color;
	current++;
}

/*
==============
SCR_DrawDebugGraph
==============
*/
void SCR_DrawDebugGraph (void)
{
	int		a, x, y, w, i, h;
	float	v;
	int		color;

	// draw the graph
	w = viddef.conwidth;
	x = 0;
	y = viddef.conheight;

	re.DrawFill (x, y - scr_graphheight->value, w, scr_graphheight->value, 8);

	for (a = 0; a < w; a++)
	{
		i = (current - 1 - a + 1024) & 1023;
		v = values[i].value;
		color = values[i].color;
		v = v * scr_graphscale->value + scr_graphshift->value;

		if (v < 0)
			v += scr_graphheight->value * (1 + (int) (-v / scr_graphheight->value));

		h = (int) v % (int) scr_graphheight->value;
		re.DrawFill (x + w - 1 - a, y - h, 1, h, color);
	}
}


/*
===============================================================================

CENTER PRINTING

===============================================================================
*/

char		scr_centerstring[4096];
int			scr_centertime_off;
int			scr_center_lines;


/*
==============
SCR_CenterPrint

Called for important messages that should stay in the center of the screen
for a few moments
==============
*/
void SCR_CenterPrint (char *str)
{
	char	*s;
	char	line[80];
	int		i, j, l;

	strncpy (scr_centerstring, str, sizeof (scr_centerstring) - 1);
	scr_centertime_off = cl.time + (int) (scr_centertime->value * 1000);

	// count the number of lines for centering
	scr_center_lines = 1;
	s = str;

	while (*s)
	{
		if (*s == '\n')
			scr_center_lines++;
		s++;
	}

	// echo it to the console
	Com_Printf ("\n\n\35\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\37\n\n");
	s = str;

	do
	{
		// scan the width of the line
		for (l = 0; l < 40; l++)
			if (s[l] == '\n' || !s[l])
				break;

		for (i = 0; i < (40 - l) / 2; i++)
			line[i] = ' ';

		for (j = 0; j < l; j++)
			line[i++] = s[j];

		line[i] = '\n';
		line[i + 1] = 0;

		Com_Printf ("%s", line);

		while (*s && *s != '\n')
			s++;

		if (!*s)
			break;

		s++;		// skip the \n
	} while (1);

	Com_Printf ("\n\n\35\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\37\n\n");
	Con_ClearNotify ();
}


void SCR_DrawCenterString (void)
{
	int		l;
	int		j;
	int		x;

	char *start = scr_centerstring;
	int y = viddef.conheight * 0.35 - (scr_center_lines * 4);

	do
	{
		// scan the width of the line
		for (l = 0; l < 40; l++)
			if (start[l] == '\n' || !start[l])
				break;

		x = (viddef.conwidth - l * 8) / 2;

		for (j = 0; j < l; j++, x += 8)
			re.DrawChar (x, y, start[j]);

		y += 8;

		while (*start && *start != '\n')
			start++;

		if (!*start)
			break;

		start++;		// skip the \n
	} while (1);

	re.DrawString ();
}


void SCR_ClearCenterString (void)
{
	scr_centertime_off = 0;
	scr_center_lines = 0;
	scr_centerstring[0] = 0;
}


void SCR_CheckDrawCenterString (void)
{
	// remove any prior centerprints
	if (cls.state != ca_active || !cl.refresh_prepped)
	{
		SCR_ClearCenterString ();
		return;
	}

	if (!scr_centerstring[0])
	{
		SCR_ClearCenterString ();
		return;
	}

	if (cl.time >= scr_centertime_off)
	{
		SCR_ClearCenterString ();
		return;
	}

	SCR_DrawCenterString ();
}


//=============================================================================


/*
=================
SCR_SizeUp_f

Keybinding command
=================
*/
void SCR_SizeUp_f (void)
{
}


/*
=================
SCR_SizeDown_f

Keybinding command
=================
*/
void SCR_SizeDown_f (void)
{
}

/*
=================
SCR_Sky_f

Set a specific sky and rotation speed
=================
*/
void SCR_Sky_f (void)
{
	float	rotate = 0;
	vec3_t	axis = {0, 0, 1};

	if (Cmd_Argc () < 2)
	{
		Com_Printf ("Usage: sky <basename> <rotate> <axis x y z>\n");
		return;
	}

	if (Cmd_Argc () > 2)
		rotate = atof (Cmd_Argv (2));

	if (Cmd_Argc () == 6)
	{
		axis[0] = atof (Cmd_Argv (3));
		axis[1] = atof (Cmd_Argv (4));
		axis[2] = atof (Cmd_Argv (5));
	}

	re.SetSky (Cmd_Argv (1), rotate, axis);
}


static void SCR_PerformScreenshot (char *shotname, int extraflags)
{
	if (strstr (shotname, ".."))
	{
		Com_Printf ("SCR_PerformScreenshot : refusing to use a path with \"..\"\n");
		return;
	}

	// refresh the screen without any gamma or brightness applied; do not swap buffers, etc
	SCR_UpdateScreen (SCR_NO_GAMMA | SCR_NO_BRIGHTNESS | SCR_NO_PRESENT | SCR_SYNC_PIPELINE | extraflags);

	// and capture it to our screenshot
	re.CaptureScreenshot (shotname);

	// then do a normal refresh to wipe out what we just did
	SCR_UpdateScreen (SCR_DEFAULT);
}


void SCR_Screenshot_f (void)
{
#define SHOTDIR "scrnshot"

	// create the scrnshots directory if it doesn't exist
	Sys_Mkdir (va ("%s/"SHOTDIR, FS_Gamedir ()));

	if (Cmd_Argc () < 2)
	{
		int i;

		// find a file name to save it to
		for (i = 0; i <= 99; i++)
		{
			FILE *f;
			char *checkname = va ("%s/"SHOTDIR"/quake%02i.tga", FS_Gamedir (), i);

			if ((f = fopen (checkname, "rb")) == NULL)
			{
				// create the scheenshot
				SCR_PerformScreenshot (checkname, SCR_DEFAULT);
				return;
			}

			fclose (f);
		}

		// didn't do it
		Com_Printf ("SCR_ScreenShot_f: Couldn't create a file\n");
	}
	else
	{
		// using the first param as a custom shot name
		char *checkname = va ("%s/"SHOTDIR"/%s.tga", FS_Gamedir (), Cmd_Argv (1));
		SCR_PerformScreenshot (checkname, SCR_NO_2D_UI);
	}
}


//============================================================================

/*
==================
SCR_Init
==================
*/
void SCR_Init (void)
{
	scr_conspeed = Cvar_Get ("scr_conspeed", "3", 0, NULL);
	scr_showturtle = Cvar_Get ("scr_showturtle", "0", 0, NULL);
	scr_showpause = Cvar_Get ("scr_showpause", "1", 0, NULL);
	scr_centertime = Cvar_Get ("scr_centertime", "2.5", 0, NULL);
	scr_printspeed = Cvar_Get ("scr_printspeed", "8", 0, NULL);
	scr_netgraph = Cvar_Get ("netgraph", "0", 0, NULL);
	scr_timegraph = Cvar_Get ("timegraph", "0", 0, NULL);
	scr_debuggraph = Cvar_Get ("debuggraph", "0", 0, NULL);
	scr_graphheight = Cvar_Get ("graphheight", "32", 0, NULL);
	scr_graphscale = Cvar_Get ("graphscale", "1", 0, NULL);
	scr_graphshift = Cvar_Get ("graphshift", "0", 0, NULL);

	// register our commands
	Cmd_AddCommand ("timerefresh", SCR_TimeRefresh_f);
	Cmd_AddCommand ("loading", SCR_Loading_f);
	Cmd_AddCommand ("sizeup", SCR_SizeUp_f);
	Cmd_AddCommand ("sizedown", SCR_SizeDown_f);
	Cmd_AddCommand ("sky", SCR_Sky_f);
	Cmd_AddCommand ("screenshot", SCR_Screenshot_f);

	scr_initialized = true;
}


/*
==============
SCR_DrawNet
==============
*/
void SCR_DrawNet (void)
{
	if (cls.netchan.outgoing_sequence - cls.netchan.incoming_acknowledged < CMD_BACKUP - 1)
		return;

	re.DrawPic (64, 0, "net");
}


/*
==============
SCR_DrawPause
==============
*/
void SCR_DrawPause (void)
{
	int		w, h;

	if (!scr_showpause->value)		// turn off for screenshots
		return;

	if (!cl_paused->value)
		return;

	re.DrawGetPicSize (&w, &h, "pause");
	re.DrawPic ((viddef.conwidth - w) / 2, viddef.conheight / 2 + 8, "pause");
}


/*
==============
SCR_DrawLoading
==============
*/
void SCR_DrawLoading (void)
{
	int		w, h;

	if (!scr_draw_loading)
		return;

	scr_draw_loading = false;
	re.DrawGetPicSize (&w, &h, "loading");
	re.DrawPic ((viddef.conwidth - w) / 2, (viddef.conheight - h) / 2, "loading");
}

//=============================================================================

/*
==================
SCR_RunConsole

Scroll it up or down
==================
*/
void SCR_RunConsole (void)
{
	static int con_oldtime = -1;
	float con_frametime;

	// check for first call
	if (con_oldtime < 0) con_oldtime = cls.realtime;

	// get correct frametime
	con_frametime = (float) (cls.realtime - con_oldtime) * 0.001f;
	con_oldtime = cls.realtime;

	// decide on the height of the console
	if (cls.key_dest == key_console)
		scr_conlines = 0.5;		// half screen
	else scr_conlines = 0;		// none visible

	if (scr_conlines < scr_con_current)
	{
		scr_con_current -= scr_conspeed->value * (100.0f / 200.0f) * con_frametime;

		if (scr_conlines > scr_con_current)
			scr_con_current = scr_conlines;
	}
	else if (scr_conlines > scr_con_current)
	{
		scr_con_current += scr_conspeed->value * (100.0f / 200.0f) * con_frametime;

		if (scr_conlines < scr_con_current)
			scr_con_current = scr_conlines;
	}
}


/*
==================
SCR_DrawConsole
==================
*/
void SCR_DrawConsole (void)
{
	Con_CheckResize ();

	if (cls.state == ca_disconnected || cls.state == ca_connecting)
	{
		// forced full screen console
		if (cls.key_dest == key_menu)
			re.Clear ();
		else Con_DrawConsole (1.0f, 255);
		return;
	}

	if (cls.state != ca_active || !cl.refresh_prepped)
	{
		// connected, but can't render
		re.Clear ();
		Con_DrawConsole (0.5f, 255);
		return;
	}

	if (scr_con_current)
	{
		if (cls.key_dest == key_menu && scr_con_current > 0.999f)
			re.Clear ();
		else if (cls.key_dest != key_menu)
			Con_DrawConsole (scr_con_current, (int) (scr_con_current * 320.0f));
	}
	else
	{
		if (cls.key_dest == key_game || cls.key_dest == key_message)
			Con_DrawNotify ();	// only draw notify in game
	}
}

//=============================================================================

/*
================
SCR_BeginLoadingPlaque
================
*/
void SCR_BeginLoadingPlaque (void)
{
	S_StopAllSounds ();
	cl.sound_prepped = false;		// don't play ambients
	CDAudio_Stop ();

	if (cls.disable_screen) return;
	if (developer->value) return;
	if (cls.state == ca_disconnected) return;	// if at console, don't bring up the plaque
	if (cls.key_dest == key_console) return;

	if (cl.cinematictime > 0)
		scr_draw_loading = 2;	// clear to black first
	else scr_draw_loading = 1;

	SCR_UpdateScreen (SCR_NO_VSYNC);
	cls.disable_screen = Sys_Milliseconds ();
	cls.disable_servercount = cl.servercount;
}

/*
================
SCR_EndLoadingPlaque
================
*/
void SCR_EndLoadingPlaque (void)
{
	cls.disable_screen = 0;
	Con_ClearNotify ();
}


/*
================
SCR_Loading_f
================
*/
void SCR_Loading_f (void)
{
	SCR_BeginLoadingPlaque ();
}


/*
================
SCR_TimeRefresh_f
================
*/
int entitycmpfnc (const entity_t *a, const entity_t *b)
{
	// all other models are sorted by model then skin
	if (a->model == b->model)
		return ((int) a->skin - (int) b->skin);
	else return ((int) a->model - (int) b->model);
}


void SCR_TimeRefresh_f (void)
{
	int		i;
	int		start, stop;
	float	startangle, time;
	int		timeRefreshTime = 1800;

	if (cls.state != ca_active)
		return;

	startangle = cl.refdef.viewangles[1];
	start = Sys_Milliseconds ();

	// do a 360 in 1.8 seconds
	for (i = 0;; i++)
	{
		cl.refdef.viewangles[1] = startangle + (float) (Sys_Milliseconds () - start) * (360.0f / timeRefreshTime);

		re.BeginFrame (&viddef, SCR_DEFAULT);
		re.RenderFrame (&cl.refdef);
		re.EndFrame (SCR_NO_VSYNC);

		if ((time = Sys_Milliseconds () - start) >= timeRefreshTime) break;
	}

	stop = Sys_Milliseconds ();
	cl.refdef.viewangles[1] = startangle;
	time = (stop - start) / 1000.0;
	Com_Printf ("%i frames, %f seconds (%f fps)\n", i, time, (float) i / time);
}


//===============================================================


/*
===============
SCR_TouchPics

Allows rendering code to cache all needed sbar graphics
===============
*/
void SCR_TouchPics (void)
{
	if (crosshair->value)
	{
		if (crosshair->value > 3 || crosshair->value < 0)
			crosshair->value = 3;

		Com_sprintf (crosshair_pic, sizeof (crosshair_pic), "ch%i", (int) (crosshair->value));
		re.DrawGetPicSize (&crosshair_width, &crosshair_height, crosshair_pic);

		if (!crosshair_width)
			crosshair_pic[0] = 0;
	}
}


/*
================
SCR_DrawStats

The status bar is a small layout program that
is based on the stats array
================
*/
void SCR_DrawStats (void)
{
	SCR_ExecuteLayoutString (cl.configstrings[CS_STATUSBAR]);
}


/*
================
SCR_DrawLayout

================
*/
#define	STAT_LAYOUTS		13

void SCR_DrawLayout (void)
{
	if (!cl.frame.playerstate.stats[STAT_LAYOUTS])
		return;
	SCR_ExecuteLayoutString (cl.layout);
}

//=======================================================

/*
==================
SCR_UpdateScreen

This is called every frame, and can also be called explicitly to flush
text to the screen.
==================
*/
void CL_DrawFPS (void);

void SCR_UpdateScreen (int scrflags)
{
	// if the screen is disabled (loading plaque is up, or vid mode changing) do nothing at all
	if (cls.disable_screen)
	{
		if (Sys_Milliseconds () - cls.disable_screen > 120000)
		{
			cls.disable_screen = 0;
			Com_Printf ("Loading plaque timed out.\n");
		}
		return;
	}

	if (!scr_initialized || !con.initialized)
		return;				// not initialized yet

	re.BeginFrame (&viddef, scrflags);

	if (scr_draw_loading == 2)
	{
		//  loading plaque over black screen
		int	w, h;

		scr_draw_loading = false;

		re.Set2D ();
		re.Clear (); // this was never done
		re.DrawGetPicSize (&w, &h, "loading");
		re.DrawPic ((viddef.conwidth - w) / 2, (viddef.conheight - h) / 2, "loading");
	}
	else if (cl.cinematictime > 0)
	{
		re.Set2D ();
		re.Clear (); // always

		// if a cinematic is supposed to be running, handle menus
		// and console specially
		if (cls.key_dest == key_menu)
			M_Draw ();
		else if (cls.key_dest == key_console)
			SCR_DrawConsole ();
		else SCR_DrawCinematic ();
	}
	else
	{
		// do 3D refresh drawing, and then update the screen
		V_RenderView ();

		if (!(scrflags & SCR_NO_2D_UI))
		{
			re.Set2D ();

			SCR_DrawCrosshair (); // moved this here from V_RenderView

			CL_DrawFPS ();

			SCR_DrawStats ();

			if (cl.frame.playerstate.stats[STAT_LAYOUTS] & 1) SCR_DrawLayout ();
			if (cl.frame.playerstate.stats[STAT_LAYOUTS] & 2) CL_DrawInventory ();

			SCR_DrawNet ();
			SCR_CheckDrawCenterString ();

			if (scr_timegraph->value)
				SCR_DebugGraph (cls.frametime * 300, 0);

			if (scr_debuggraph->value || scr_timegraph->value || scr_netgraph->value)
				SCR_DrawDebugGraph ();

			SCR_DrawPause ();

			SCR_DrawConsole ();

			M_Draw ();

			SCR_DrawLoading ();
		}
	}

	// never vsync if we're in a timedemo
	if (cl_timedemo->value)
		re.EndFrame (scrflags | SCR_NO_VSYNC);
	else re.EndFrame (scrflags);
}


