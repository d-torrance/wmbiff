/* dock.c- built-in Dock module for WindowMaker
 * 
 *  WindowMaker window manager
 * 
 *  Copyright (c) 1997 Alfredo K. Kojima
 * 
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <stdlib.h>
#include <string.h>
#include "list.h"
#include "misc.h"

extern pid_t execCommand(char *command)
{
	pid_t pid;

	if ((pid = fork()) == 0) {
		execl("/bin/sh", "sh", "-c", command, (char *) 0);
	}
	return pid;
}
