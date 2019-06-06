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
// Main windowed and fullscreen graphics interface module. This module
// is used for both the software and OpenGL rendering versions of the
// Quake refresh engine.
#include <assert.h>
#include <float.h>

#include "client.h"
#include <windows.h>

// Structure containing functions exported from refresh DLL
refexport_t	re;

// Console variables that we need to access from this module
cvar_t		*vid_gamma;
cvar_t		*vid_brightness;
cvar_t		*vid_xpos;			// X coordinate of window position
cvar_t		*vid_ypos;			// Y coordinate of window position
cvar_t		*vid_fullscreen;

cvar_t		*vid_width;
cvar_t		*vid_height;

// Global variables used internally by this module
viddef_t	viddef;				// global video state; used by other modules
qboolean reflib_active = false; // true if the refresh has been loaded successfully


HWND        cl_hwnd;            // Main window handle for life of program

extern	unsigned	sys_msg_time;
extern qboolean		ActiveApp, Minimized;


/*
==========================================================================

DLL GLUE

==========================================================================
*/

#define	MAXPRINTMSG	4096

void VID_Printf (int print_level, char *fmt, ...)
{
	va_list		argptr;
	char		msg[MAXPRINTMSG];
	static qboolean	inupdate;

	va_start (argptr, fmt);
	vsprintf (msg, fmt, argptr);
	va_end (argptr);

	if (print_level == PRINT_ALL)
	{
		Com_Printf ("%s", msg);
	}
	else if (print_level == PRINT_DEVELOPER)
	{
		Com_DPrintf ("%s", msg);
	}
	else if (print_level == PRINT_ALERT)
	{
		MessageBox (0, msg, "PRINT_ALERT", MB_ICONWARNING);
		OutputDebugString (msg);
	}
}

void VID_Error (int err_level, char *fmt, ...)
{
	va_list		argptr;
	char		msg[MAXPRINTMSG];
	static qboolean	inupdate;

	va_start (argptr, fmt);
	vsprintf (msg, fmt, argptr);
	va_end (argptr);

	Com_Error (err_level, "%s", msg);
}


//==========================================================================


void AppActivate (BOOL fActive, BOOL minimize)
{
	Minimized = minimize;

	Key_ClearStates ();

	// we don't want to act like we're active if we're minimized
	if (fActive && !Minimized)
		ActiveApp = true;
	else
		ActiveApp = false;

	// minimize/restore mouse-capture on demand
	if (!ActiveApp)
	{
		IN_Activate (false);
		CDAudio_Activate (false);
		S_Activate (false);
	}
	else
	{
		IN_Activate (true);
		CDAudio_Activate (true);
		S_Activate (true);
	}
}

/*
====================
MainWndProc

main window procedure
====================
*/
LONG CDAudio_MessageHandler (HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
BOOL IN_InputProc (HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

LONG WINAPI MainWndProc (HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	// look for input events
	if (IN_InputProc (hWnd, uMsg, wParam, lParam)) return 0;

	switch (uMsg)
	{
	case WM_ERASEBKGND:
		return 1;

	case WM_CREATE:
		cl_hwnd = hWnd;

		// bring the AppActivate flags up to date
		AppActivate (true, false);

		return DefWindowProc (hWnd, uMsg, wParam, lParam);

	case WM_PAINT:
		return DefWindowProc (hWnd, uMsg, wParam, lParam);

	case WM_DESTROY:
		// let sound and input know about this?
		cl_hwnd = NULL;
		return DefWindowProc (hWnd, uMsg, wParam, lParam);

	case WM_ACTIVATE:
	{
		// KJB: Watch this for problems in fullscreen modes with Alt-tabbing.
		int fActive = LOWORD (wParam);
		int fMinimized = (BOOL) HIWORD (wParam);

		AppActivate (fActive != WA_INACTIVE, fMinimized);

		if (reflib_active)
			re.AppActivate (!(fActive == WA_INACTIVE));
	}
	return DefWindowProc (hWnd, uMsg, wParam, lParam);

	case WM_MOVE:
		if (!vid_fullscreen->value)
		{
			int xPos = (short) LOWORD (lParam);    // horizontal position 
			int yPos = (short) HIWORD (lParam);    // vertical position 
			RECT r; int style;

			r.left = 0;
			r.top = 0;
			r.right = 1;
			r.bottom = 1;

			style = GetWindowLong (hWnd, GWL_STYLE);
			AdjustWindowRect (&r, style, FALSE);

			Cvar_SetValue ("vid_xpos", xPos + r.left);
			Cvar_SetValue ("vid_ypos", yPos + r.top);
			vid_xpos->modified = false;
			vid_ypos->modified = false;
			if (ActiveApp)
				IN_Activate (true);
		}

		break;

	case WM_SYSCOMMAND:
		if (wParam == SC_SCREENSAVE)
			return 0;
		return DefWindowProc (hWnd, uMsg, wParam, lParam);

	case MM_MCINOTIFY:
		return CDAudio_MessageHandler (hWnd, uMsg, wParam, lParam);

	default:
		break;
	}

	return DefWindowProc (hWnd, uMsg, wParam, lParam);
}


void VID_Front_f (void)
{
	SetWindowLong (cl_hwnd, GWL_EXSTYLE, WS_EX_TOPMOST);
	SetForegroundWindow (cl_hwnd);
}

int RectWidth (const RECT *r) { return r->right - r->left; }
int RectHeight (const RECT *r) { return r->bottom - r->top; }

void VID_CenterWindow_f (void)
{
	RECT windowrect;
	RECT workarea;

	GetWindowRect (cl_hwnd, &windowrect);
	SystemParametersInfo (SPI_GETWORKAREA, 0, &workarea, 0);

	MoveWindow (
		cl_hwnd,
		workarea.left + (RectWidth (&workarea) - RectWidth (&windowrect)) / 2,
		workarea.top + (RectHeight (&workarea) - RectHeight (&windowrect)) / 2,
		RectWidth (&windowrect),
		RectHeight (&windowrect),
		TRUE
	);
}


/*
==============
VID_UpdateWindowPosAndSize
==============
*/
void VID_UpdateWindowPosAndSize (int x, int y)
{
	RECT r;
	int		style;
	int		w, h;

	r.left = 0;
	r.top = 0;
	r.right = viddef.width;
	r.bottom = viddef.height;

	style = GetWindowLong (cl_hwnd, GWL_STYLE);
	AdjustWindowRect (&r, style, FALSE);

	w = r.right - r.left;
	h = r.bottom - r.top;

	MoveWindow (cl_hwnd, vid_xpos->value, vid_ypos->value, w, h, TRUE);
}

/*
==============
VID_NewWindow
==============
*/
void VID_NewWindow (void)
{
	cl.force_refdef = true;		// can't use a paused refdef
}


void VID_FreeReflib (void)
{
	memset (&re, 0, sizeof (re));
	reflib_active = false;
}


/*
==============
VID_LoadRefresh
==============
*/
void Sys_SetupMemoryRefImports (refimport_t	*ri);

qboolean VID_LoadRefresh (void)
{
	refimport_t	ri;

	if (reflib_active)
	{
		re.Shutdown ();
		VID_FreeReflib ();
	}

	Com_Printf ("------- Loading refresh -------\n");

	ri.Cmd_AddCommand = Cmd_AddCommand;
	ri.Cmd_RemoveCommand = Cmd_RemoveCommand;
	ri.Cmd_Argc = Cmd_Argc;
	ri.Cmd_Argv = Cmd_Argv;
	ri.Cmd_ExecuteText = Cbuf_ExecuteText;
	ri.Con_Printf = VID_Printf;
	ri.Sys_Error = VID_Error;
	ri.Mkdir = Sys_Mkdir;
	ri.SendKeyEvents = Sys_SendKeyEvents;
	ri.FS_LoadFile = FS_LoadFile;
	ri.FS_FreeFile = FS_FreeFile;
	ri.FS_Gamedir = FS_Gamedir;
	ri.Cvar_Get = Cvar_Get;
	ri.Cvar_Set = Cvar_Set;
	ri.Cvar_SetValue = Cvar_SetValue;
	ri.Vid_MenuInit = VID_MenuInit;
	ri.Vid_PrepVideoMenu = VID_PrepVideoMenu;
	ri.Vid_NewWindow = VID_NewWindow;

	Sys_SetupMemoryRefImports (&ri);

	re = GetRefAPI (ri);

	if (re.api_version != API_VERSION)
	{
		VID_FreeReflib ();
		Com_Error (ERR_FATAL, "refresh has incompatible api_version");
	}

	// enumerate the video modes before bringing stuff on so that we have a valid list of modes before we create the window, device or swapchain
	re.EnumerateVideoModes ();

	if (re.Init (GetModuleHandle (NULL), MainWndProc) == -1)
	{
		re.Shutdown ();
		VID_FreeReflib ();
		return false;
	}

	Com_Printf ("------------------------------------\n");
	reflib_active = true;

	return true;
}

/*
============
VID_ResetMode

This function gets called once just before drawing each frame, and it's sole purpose in life
is to check to see if any of the video mode parameters have changed, and if they have to
update the rendering DLL and/or video mode to match.
============
*/
void VID_ResetMode (void)
{
	// can't use a paused refdef
	cl.force_refdef = true;

	// don't loop sounds while resetting
	S_StopAllSounds ();

	// refresh has changed
	cl.refresh_prepped = false;
	cls.disable_screen = true;

	if (!VID_LoadRefresh ())
	{
		Com_Error (ERR_FATAL, "VID_LoadRefresh failed!");
		return;
	}

	// update our window position
	if (vid_xpos->modified || vid_ypos->modified)
	{
		if (!vid_fullscreen->value)
			VID_UpdateWindowPosAndSize (vid_xpos->value, vid_ypos->value);

		vid_xpos->modified = false;
		vid_ypos->modified = false;
	}

	if (!vid_fullscreen->value)
	{
		// center the window, which seems reasonable to do after switching modes
		VID_CenterWindow_f ();

		// bring the screen to topmost, which seems a safe assumption after resetting the mode
		VID_Front_f ();
	}

	// we can draw on the screen now
	cls.disable_screen = false;
}

/*
============
VID_Init
============
*/
void VID_Init (void)
{
	// Create the video variables so we know how to start the graphics drivers
	vid_xpos = Cvar_Get ("vid_xpos", "3", CVAR_ARCHIVE, NULL);
	vid_ypos = Cvar_Get ("vid_ypos", "22", CVAR_ARCHIVE, NULL);
	vid_fullscreen = Cvar_Get ("vid_fullscreen", "0", CVAR_ARCHIVE | CVAR_VIDEO, NULL);
	vid_gamma = Cvar_Get ("vid_gamma", "1", CVAR_ARCHIVE, NULL);
	vid_brightness = Cvar_Get ("vid_brightness", "1", CVAR_ARCHIVE, NULL);

	vid_width = Cvar_Get ("vid_width", "640", CVAR_ARCHIVE | CVAR_VIDEO, NULL);
	vid_height = Cvar_Get ("vid_height", "480", CVAR_ARCHIVE | CVAR_VIDEO, NULL);

	// Add some console commands that we want to handle
	Cmd_AddCommand ("vid_restart", VID_ResetMode);
	Cmd_AddCommand ("vid_front", VID_Front_f);
	Cmd_AddCommand ("centerwindow", VID_CenterWindow_f);

	// Start the graphics mode and load refresh DLL
	VID_ResetMode ();
}


/*
============
VID_Shutdown
============
*/
void VID_Shutdown (void)
{
	if (reflib_active)
	{
		re.Shutdown ();
		VID_FreeReflib ();
	}
}


