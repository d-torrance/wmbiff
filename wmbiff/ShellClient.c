/* Author: Beno�t Rouits ( brouits@free.fr ) thanks to Neil Spring.
   from LicqClient by Yong-iL Joh ( tolkien@mizi.com ) 
   and Jorge Garc�a ( Jorge.Garcia@uv.es )
 * 
 * generic Shell command support
 *
 * Last Updated : Tue Mar  5 15:23:35 CET 2002
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "Client.h"
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <signal.h>
#include <assert.h>
#include <strings.h>
#include "charutil.h"
#ifdef USE_DMALLOC
#include <dmalloc.h>
#endif

#define SH_DM(pc, lvl, args...) DM(pc, lvl, "shell: " args)

/* kind_popen bumps off the sigchld handler - we care whether
   a checking program fails. */

#ifdef __LCLINT__
void (*old_signal_handler) (int);
#else
RETSIGTYPE(*old_signal_handler) (int);
#endif

/*@null@*/
FILE *kind_popen(const char *command, const char *type)
{
	FILE *ret;
	assert(strcmp(type, "r") == 0);
	assert(old_signal_handler == NULL);
	old_signal_handler = signal(SIGCHLD, SIG_DFL);
	ret = popen(command, type);
	if (ret == NULL) {
		DMA(DEBUG_ERROR, "popen: error while reading '%s': %s\n",
			command, strerror(errno));
		(void) signal(SIGCHLD, old_signal_handler);
		old_signal_handler = NULL;
	}
	return (ret);
}

/* kind_pclose checks the return value from pclose and prints
   some nice error messages about it.  ordinarily, this would be 
   a good idea, but wmbiff has a sigchld handler that reaps 
   children immediately (needed when spawning other child processes),
   so no error checking can be done here until that's disabled */

/* returns as a mailcheck function does: -1 on fail, 0 on success */
static int kind_pclose( /*@only@ */ FILE * F,
					   const char *command,
					   /*@null@ */ Pop3 pc)
{
	int exit_status = pclose(F);

	if (old_signal_handler != NULL) {
		(void) signal(SIGCHLD, old_signal_handler);
		old_signal_handler = NULL;
	}

	if (exit_status != 0) {
		if (exit_status == -1) {
			/* wmbiff has a sigchld handler already, so wait is likely 
			   to fail */
			SH_DM(pc, DEBUG_ERROR, "pclose '%s' failed: %s\n",
				  command, strerror(errno));
		} else {
			SH_DM(pc, DEBUG_ERROR,
				  "'%s' exited with non-zero status %d\n", command,
				  exit_status);
		}
	}
	return (exit_status);
}

int grabCommandOutput(Pop3 pc, const char *command,	/*@out@ */
					  char **output)
{
	FILE *F;
	char linebuf[512];
	SH_DM(pc, DEBUG_INFO, "Executing '%s'\n", command);
	*output = NULL;
	if ((F = kind_popen(command, "r")) == NULL) {
		return -1;
	}
	if (fgets(linebuf, 512, F) == NULL) {
		SH_DM(pc, DEBUG_ERROR,
			  "fgets: unable to read the output of '%s': %s\n", command,
			  strerror(errno));
	} else {
		chomp(linebuf);
		*output = strdup_ordie(linebuf);
	}
	return (kind_pclose(F, command, pc));
}

/* returns null on failure */
/*@null@*/
char *backtickExpand(Pop3 pc, const char *path)
{
	char bigbuffer[1024];
	const char *tickstart;
	const char *tickend;
	bigbuffer[0] = '\0';
	while ((tickstart = strchr(path, '`')) != NULL) {
		char *command;
		char *commandoutput;
		tickend = strchr(tickstart + 1, '`');
		if (tickend == NULL) {
			SH_DM(pc, DEBUG_ERROR, "unbalanced \' in %s\n", path);
			return NULL;
		}
		strncat(bigbuffer, path, tickstart - path);
		command = strdup_ordie(tickstart + 1);
		command[tickend - tickstart - 1] = '\0';
		(void) grabCommandOutput(pc, command, &commandoutput);
		free(command);
		if (commandoutput != NULL) {
			strcat(bigbuffer, commandoutput);
			free(commandoutput);
		}
		path = tickend + 1;
	}
	/* grab the rest */
	strcat(bigbuffer, path);
	SH_DM(pc, DEBUG_INFO, "expanded to %s\n", bigbuffer);
	return (strdup_ordie(bigbuffer));
}

int shellCmdCheck(Pop3 pc)
{
	int count_status = 0;
	char *commandOutput;

	if (pc == NULL)
		return -1;
	SH_DM(pc, DEBUG_INFO, ">Mailbox: '%s'\n", pc->path);

	/* fetch the first line of input */
	pc->TextStatus[0] = '\0';
	(void) grabCommandOutput(pc, pc->path, &commandOutput);
	if (commandOutput == NULL) {
		return -1;
	}
	SH_DM(pc, DEBUG_INFO, "'%s' returned '%s'\n", pc->path, commandOutput);

	/* see if it's numeric; the numeric check is somewhat 
	   useful, as wmbiff renders 4-digit numbers, but not
	   4-character strings. */
	if (sscanf(commandOutput, "%d", &(count_status)) == 1) {
		if (strstr(commandOutput, "new")) {
			pc->UnreadMsgs = count_status;
			pc->TotalMsgs = 0;
		} else if (strstr(commandOutput, "old")) {
			pc->UnreadMsgs = 0;
			pc->TotalMsgs = count_status;
		} else {
			/* this default should be configurable. */
			pc->UnreadMsgs = 0;
			pc->TotalMsgs = count_status;
		}
	} else if (sscanf(commandOutput, "%9s\n", pc->TextStatus) == 1) {
		/* validate the string input */
		int i;
		for (i = 0; pc->TextStatus[i] != '\0' && isalnum(pc->TextStatus[i])
			 && i < 10; i++);
		if (pc->TextStatus[i] != '\0') {
			SH_DM(pc, DEBUG_ERROR,
				  "wmbiff only supports alphanumeric (isalnum) strings:\n"
				  " '%s' is not ok\n", pc->TextStatus);
			/* null terminate it at the first bad char: */
			pc->TextStatus[i] = '\0';
		}
		/* see if we should print as new or not */
		pc->UnreadMsgs = (strstr(commandOutput, "new")) ? 1 : 0;
		pc->TotalMsgs = -1;		/* we might alternat numeric /string */
	} else {
		SH_DM(pc, DEBUG_ERROR,
			  "'%s' returned something other than an integer message count"
			  " or short string.\n", pc->path);
		free(commandOutput);
		return -1;
	}

	SH_DM(pc, DEBUG_INFO, "from: %s status: %s %d %d\n",
		  pc->path, pc->TextStatus, pc->TotalMsgs, pc->UnreadMsgs);
	free(commandOutput);
	return (0);
}

int shellCreate( /*@notnull@ */ Pop3 pc, const char *str)
{
	/* SHELL format: shell:::/path/to/script */
	const char *reserved1, *reserved2, *commandline;

	pc->TotalMsgs = 0;
	pc->UnreadMsgs = 0;
	pc->OldMsgs = -1;
	pc->OldUnreadMsgs = -1;
	pc->checkMail = shellCmdCheck;
	reserved1 = str + 6;		/* shell:>:: */

	assert(strncasecmp("shell:", str, 6) == 0);

	reserved2 = index(reserved1, ':');
	if (reserved2 == NULL) {
		SH_DM(pc, DEBUG_ERROR, "unable to parse '%s', expecting ':'", str);
		return 0;
	}
	reserved2++;				/* shell::>: */

	commandline = index(reserved2, ':');
	if (commandline == NULL) {
		SH_DM(pc, DEBUG_ERROR,
			  "unable to parse '%s', expecting another ':'", str);
		return 0;
	}
	commandline++;				/* shell:::> */

	/* good thing strcpy handles overlapping regions */
	SH_DM(pc, DEBUG_INFO, "path= '%s'\n", commandline);
	strcpy(pc->path, commandline);
	return 0;
}

/* vim:set ts=4: */
/*
 * Local Variables:
 * tab-width: 4
 * c-indent-level: 4
 * c-basic-offset: 4
 * End:
 */
