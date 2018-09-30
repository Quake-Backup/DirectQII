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

#include "qcommon.h"
#include <windows.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <direct.h>
#include <io.h>
#include <conio.h>


/*
================
Sys_Milliseconds
================
*/
// time returned by last Sys_Milliseconds
// note - the intent is clearly that this be only called once per-frame and then everything can advance with the same view of time
int	sys_currmsec;

int Sys_Milliseconds (void)
{
	static qboolean first = true;
	static __int64 qpcstart = 0;
	static __int64 qpcfreq = 0;
	__int64 qpcnow = 0;

	if (first)
	{
		first = false;

		QueryPerformanceCounter ((LARGE_INTEGER *) &qpcstart);
		QueryPerformanceFrequency ((LARGE_INTEGER *) &qpcfreq);

		return 0;
	}

	QueryPerformanceCounter ((LARGE_INTEGER *) &qpcnow);

	return ((qpcnow - qpcstart) * 1000 + (qpcfreq >> 1)) / qpcfreq;
}


void Sys_Mkdir (char *path)
{
	_mkdir (path);
}

//============================================

char	findbase[MAX_OSPATH];
char	findpath[MAX_OSPATH];
int		findhandle;

static qboolean CompareAttributes (unsigned found, unsigned musthave, unsigned canthave)
{
	if ((found & _A_RDONLY) && (canthave & SFF_RDONLY))
		return false;
	if ((found & _A_HIDDEN) && (canthave & SFF_HIDDEN))
		return false;
	if ((found & _A_SYSTEM) && (canthave & SFF_SYSTEM))
		return false;
	if ((found & _A_SUBDIR) && (canthave & SFF_SUBDIR))
		return false;
	if ((found & _A_ARCH) && (canthave & SFF_ARCH))
		return false;

	if ((musthave & SFF_RDONLY) && !(found & _A_RDONLY))
		return false;
	if ((musthave & SFF_HIDDEN) && !(found & _A_HIDDEN))
		return false;
	if ((musthave & SFF_SYSTEM) && !(found & _A_SYSTEM))
		return false;
	if ((musthave & SFF_SUBDIR) && !(found & _A_SUBDIR))
		return false;
	if ((musthave & SFF_ARCH) && !(found & _A_ARCH))
		return false;

	return true;
}

char *Sys_FindFirst (char *path, unsigned musthave, unsigned canthave)
{
	struct _finddata_t findinfo;

	if (findhandle)
		Sys_Error ("Sys_BeginFind without close");

	findhandle = 0;

	COM_FilePath (path, findbase);
	findhandle = _findfirst (path, &findinfo);

	while ((findhandle != -1))
	{
		if (CompareAttributes (findinfo.attrib, musthave, canthave))
		{
			Com_sprintf (findpath, sizeof (findpath), "%s/%s", findbase, findinfo.name);
			return findpath;
		}
		else if (_findnext (findhandle, &findinfo) == -1)
		{
			_findclose (findhandle);
			findhandle = -1;
		}
	}

	return NULL;
}



char *Sys_FindNext (unsigned musthave, unsigned canthave)
{
	struct _finddata_t findinfo;

	if (findhandle == -1)
		return NULL;

	while (_findnext (findhandle, &findinfo) != -1)
	{
		if (CompareAttributes (findinfo.attrib, musthave, canthave))
		{
			Com_sprintf (findpath, sizeof (findpath), "%s/%s", findbase, findinfo.name);
			return findpath;
		}
	}

	return NULL;
}


void Sys_FindClose (void)
{
	if (findhandle != -1)
		_findclose (findhandle);
	findhandle = 0;
}


//============================================

