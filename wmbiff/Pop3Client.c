/* $Id: Pop3Client.c,v 1.17 2003/03/02 02:17:14 bluehal Exp $ */
/* Author : Scott Holden ( scotth@thezone.net )
   Modified : Yong-iL Joh ( tolkien@mizi.com )
   Modified : Jorge Garc�a ( Jorge.Garcia@uv.es )
   Modified ; Mark Hurley ( debian4tux@telocity.com )
   Modified : Neil Spring ( nspring@cs.washington.edu )
 * 
 * Pop3 Email checker.
 *
 * Last Updated : Tue Nov 13 13:45:23 PST 2001
 *
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "Client.h"
#include "charutil.h"
#include "regulo.h"

#ifdef USE_DMALLOC
#include <dmalloc.h>
#endif

extern int Relax;

#define	PCU	(pc->u).pop_imap
#define POP_DM(pc, lvl, args...) DM(pc, lvl, "pop3: " args)

#ifdef HAVE_GCRYPT_H
static FILE *authenticate_md5( /*@notnull@ */ Pop3 pc, FILE * fp,
							  char *unused);
static FILE *authenticate_apop( /*@notnull@ */ Pop3 pc, FILE * fp,
							   char *apop_str);
#endif
static FILE *authenticate_plaintext( /*@notnull@ */ Pop3 pc, FILE * fp,
									char *unused);

static struct authentication_method {
	const char *name;
	/* callback returns the filehandle if successful, 
	   NULL if failed */
	FILE *(*auth_callback) (Pop3 pc, FILE * fp, char *apop_str);
} auth_methods[] = {
	{
#ifdef HAVE_GCRYPT_H
	"cram-md5", authenticate_md5}, {
	"apop", authenticate_apop}, {
#endif
	"plaintext", authenticate_plaintext}, {
	NULL, NULL}
};

/*@null@*/
FILE *pop3Login(Pop3 pc)
{
	int fd;
	FILE *fp;
	char buf[BUF_SIZE];
	char apop_str[BUF_SIZE];
	char *ptr1, *ptr2;
	struct authentication_method *a;

	apop_str[0] = '\0';			/* if defined, server supports apop */

	if ((fd = sock_connect(PCU.serverName, PCU.serverPort)) == -1) {
		POP_DM(pc, DEBUG_ERROR, "Not Connected To Server '%s:%d'\n",
			   PCU.serverName, PCU.serverPort);
		return NULL;
	}

	fp = fdopen(fd, "r+");
	fgets(buf, BUF_SIZE, fp);
	fflush(fp);
	POP_DM(pc, DEBUG_INFO, "%s", buf);

	/* Detect APOP, copy challenge into apop_str */
	for (ptr1 = buf + strlen(buf), ptr2 = NULL; ptr1 > buf; --ptr1) {
		if (*ptr1 == '>') {
			ptr2 = ptr1;
		} else if (*ptr1 == '<') {
			if (ptr2) {
				*(ptr2 + 1) = 0;
				strncpy(apop_str, ptr1, BUF_SIZE);
			}
			break;
		}
	}


	/* try each authentication method in turn. */
	for (a = auth_methods; a->name != NULL; a++) {
		/* was it specified or did the user leave it up to us? */
		if (PCU.authList[0] == '\0' || strstr(PCU.authList, a->name))
			/* did it work? */
			if ((a->auth_callback(pc, fp, apop_str)) != NULL)
				return (fp);
	}

	/* if authentication worked, we won't get here */
	POP_DM(pc, DEBUG_ERROR,
		   "All Pop3 authentication methods failed for '%s@%s:%d'\n",
		   PCU.userName, PCU.serverName, PCU.serverPort);
	fprintf(fp, "QUIT\r\n");
	fclose(fp);
	return NULL;
}

int pop3CheckMail( /*@notnull@ */ Pop3 pc)
{
	FILE *f;
	int read;
	char buf[BUF_SIZE];

	f = pop3Login(pc);
	if (f == NULL)
		return -1;

	fprintf(f, "STAT\r\n");
	fflush(f);
	fgets(buf, 256, f);
	if (buf[0] != '+') {
		POP_DM(pc, DEBUG_ERROR,
			   "Error Receiving Stats '%s@%s:%d'\n",
			   PCU.userName, PCU.serverName, PCU.serverPort);
		POP_DM(pc, DEBUG_INFO, "It said: %s\n", buf);
		return -1;
	} else {
		sscanf(buf, "+OK %d", &(pc->TotalMsgs));
	}

	/*  - Updated - Mark Hurley - debian4tux@telocity.com
	 *  In compliance with RFC 1725
	 *  which removed the LAST command, any servers
	 *  which follow this spec will return:
	 *      -ERR unimplimented
	 *  We will leave it here for those servers which haven't
	 *  caught up with the spec.
	 */
	fprintf(f, "LAST\r\n");
	fflush(f);
	fgets(buf, 256, f);
	if (buf[0] != '+') {
		/* it is not an error to receive this according to RFC 1725 */
		/* no error should be returned */
		pc->UnreadMsgs = pc->TotalMsgs;
	} else {
		sscanf(buf, "+OK %d", &read);
		pc->UnreadMsgs = pc->TotalMsgs - read;
	}

	fprintf(f, "QUIT\r\n");
	fclose(f);

	return 0;
}

int pop3Create(Pop3 pc, const char *str)
{
	/* POP3 format: pop3:user:password@server[:port] */
	/* new POP3 format: pop3:user password server [port] */
	/* If 'str' line is badly formatted, wmbiff won't display the mailbox. */
	int i;
	int matchedchars;
	/* ([^: ]+) user
	   ([^@]+) or ([^ ]+) password 
	   ([^: ]+) server 
	   ([: ][0-9]+)? optional port 
	   ' *' gobbles trailing whitespace before authentication types.
	   use separate regexes for old and new types to permit
	   use of '@' in passwords
	 */
	const char *regexes[] = {
		"pop3:([^: ]{1,32}):([^@]{0,32})@([A-Za-z][-A-Za-z0-9_.]+)(:[0-9]+)?(  *([CcAaPp][-A-Za-z5 ]*))?$",
		"pop3:([^: ]{1,32}) ([^ ]{1,32}) ([A-Za-z][-A-Za-z0-9_.]+)( [0-9]+)?(  *([CcAaPp][-A-Za-z5 ]*))?$",
		//      "pop3:([^: ]{1,32}) ([^ ]{1,32}) ([^: ]+)( [0-9]+)? *",
		// "pop3:([^: ]{1,32}):([^@]{0,32})@([^: ]+)(:[0-9]+)? *",
		NULL
	};
	struct regulo regulos[] = {
		{1, PCU.userName, regulo_strcpy},
		{2, PCU.password, regulo_strcpy},
		{3, PCU.serverName, regulo_strcpy},
		{4, &PCU.serverPort, regulo_atoi},
		{6, PCU.authList, regulo_strcpy_tolower},
		{0, NULL, NULL}
	};

	if (Relax) {
		regexes[0] =
			"pop3:([^: ]{1,32}):([^@]{0,32})@([^/: ]+)(:[0-9]+)?(  *(.*))?$";
		regexes[1] =
			"pop3:([^: ]{1,32}) ([^ ]{1,32}) ([^/: ]+)( [0-9]+)?(  *(.*))?$";
	}

	/* defaults */
	PCU.serverPort = 110;
	PCU.authList[0] = '\0';

	for (matchedchars = 0, i = 0;
		 regexes[i] != NULL && matchedchars <= 0; i++) {
		matchedchars = regulo_match(regexes[i], str, regulos);
	}

	/* failed to match either regex */
	if (matchedchars <= 0) {
		pc->label[0] = '\0';
		POP_DM(pc, DEBUG_ERROR, "Couldn't parse line %s (%d)\n"
			   "  If this used to work, run wmbiff with the -relax option, and\n "
			   "  send mail to wmbiff-devel@lists.sourceforge.net with the hostname\n"
			   "  of your mail server.\n", str, matchedchars);
		return -1;
	}
	// grab_authList(str + matchedchars, PCU.authList);

	POP_DM(pc, DEBUG_INFO, "userName= '%s'\n", PCU.userName);
	POP_DM(pc, DEBUG_INFO, "password is %d chars long\n",
		   strlen(PCU.password));
	POP_DM(pc, DEBUG_INFO, "serverName= '%s'\n", PCU.serverName);
	POP_DM(pc, DEBUG_INFO, "serverPort= '%d'\n", PCU.serverPort);
	POP_DM(pc, DEBUG_INFO, "authList= '%s'\n", PCU.authList);

	pc->checkMail = pop3CheckMail;
	pc->TotalMsgs = 0;
	pc->UnreadMsgs = 0;
	pc->OldMsgs = -1;
	pc->OldUnreadMsgs = -1;
	return 0;
}


#ifdef HAVE_GCRYPT_H
static FILE *authenticate_md5(Pop3 pc,
							  FILE * fp,
							  char *apop_str __attribute__ ((unused)))
{
	char buf[BUF_SIZE];
	char buf2[BUF_SIZE];
	unsigned char *md5;
	GCRY_MD_HD gmh;

	/* See if MD5 is supported */
	fprintf(fp, "AUTH CRAM-MD5\r\n");
	fflush(fp);
	fgets(buf, BUF_SIZE, fp);
	POP_DM(pc, DEBUG_INFO, "%s", buf);

	if (buf[0] != '+' || buf[1] != ' ') {
		/* nope, not supported. */
		return NULL;
	}

	Decode_Base64(buf + 2, buf2);
	POP_DM(pc, DEBUG_INFO, "CRAM-MD5 challenge: %s\n", buf2);

	strcpy(buf, PCU.userName);
	strcat(buf, " ");


	gmh = gcry_md_open(GCRY_MD_MD5, GCRY_MD_FLAG_HMAC);
	gcry_md_setkey(gmh, PCU.password, strlen(PCU.password));
	gcry_md_write(gmh, (unsigned char *) buf2, strlen(buf2));
	gcry_md_final(gmh);
	md5 = gcry_md_read(gmh, 0);
	/* hmac_md5(buf2, strlen(buf2), PCU.password,
	   strlen(PCU.password), md5); */
	Bin2Hex(md5, 16, buf2);
	gcry_md_close(gmh);

	strcat(buf, buf2);
	POP_DM(pc, DEBUG_INFO, "CRAM-MD5 response: %s\n", buf);
	Encode_Base64(buf, buf2);

	fprintf(fp, "%s\r\n", buf2);
	fflush(fp);
	fgets(buf, BUF_SIZE, fp);

	if (!strncmp(buf, "+OK", 3))
		return fp;				/* AUTH successful */
	else {
		POP_DM(pc, DEBUG_ERROR,
			   "CRAM-MD5 AUTH failed for user '%s@%s:%d'\n",
			   PCU.userName, PCU.serverName, PCU.serverPort);
		fprintf(stderr, "It said %s", buf);
		return NULL;
	}
}

static FILE *authenticate_apop(Pop3 pc, FILE * fp, char *apop_str)
{
	GCRY_MD_HD gmh;
	char buf[BUF_SIZE];
	unsigned char *md5;

	if (apop_str[0] == '\0') {
		/* server doesn't support apop. */
		return (NULL);
	}
	POP_DM(pc, DEBUG_INFO, "APOP challenge: %s\n", apop_str);
	strcat(apop_str, PCU.password);

	gmh = gcry_md_open(GCRY_MD_MD5, 0);
	gcry_md_write(gmh, (unsigned char *) apop_str, strlen(apop_str));
	gcry_md_final(gmh);
	md5 = gcry_md_read(gmh, 0);
	Bin2Hex(md5, 16, buf);
	gcry_md_close(gmh);

	POP_DM(pc, DEBUG_INFO, "APOP response: %s %s\n", PCU.userName, buf);
	fprintf(fp, "APOP %s %s\r\n", PCU.userName, buf);
	fflush(fp);
	fgets(buf, BUF_SIZE, fp);

	if (!strncmp(buf, "+OK", 3))
		return fp;				/* AUTH successful */
	else {
		POP_DM(pc, DEBUG_ERROR,
			   "APOP AUTH failed for user '%s@%s:%d'\n",
			   PCU.userName, PCU.serverName, PCU.serverPort);
		POP_DM(pc, DEBUG_INFO, "It said %s", buf);
		return NULL;
	}
}
#endif							/* HAVE_GCRYPT_H */

/*@null@*/
static FILE *authenticate_plaintext( /*@notnull@ */ Pop3 pc,
									FILE * fp, char *apop_str
									__attribute__ ((unused)))
{
	char buf[BUF_SIZE];

	fprintf(fp, "USER %s\r\n", PCU.userName);
	fflush(fp);
	if (fgets(buf, BUF_SIZE, fp) == NULL) {
		POP_DM(pc, DEBUG_ERROR,
			   "Error reading from server authenticating '%s@%s:%d'\n",
			   PCU.userName, PCU.serverName, PCU.serverPort);
		return NULL;
	}
	if (buf[0] != '+') {
		POP_DM(pc, DEBUG_ERROR,
			   "Failed user name when authenticating '%s@%s:%d'\n",
			   PCU.userName, PCU.serverName, PCU.serverPort);
		/* deb #128863 might be easier if we printed: */
		POP_DM(pc, DEBUG_ERROR, "The server's error message was: %s\n",
			   buf);
		return NULL;
	};

	fprintf(fp, "PASS %s\r\n", PCU.password);
	fflush(fp);
	if (fgets(buf, BUF_SIZE, fp) == NULL) {
		POP_DM(pc, DEBUG_ERROR,
			   "Error reading from server (2) authenticating '%s@%s:%d'\n",
			   PCU.userName, PCU.serverName, PCU.serverPort);
		return NULL;
	}
	if (buf[0] != '+') {
		POP_DM(pc, DEBUG_ERROR,
			   "Failed password when authenticating '%s@%s:%d'\n",
			   PCU.userName, PCU.serverName, PCU.serverPort);
		POP_DM(pc, DEBUG_ERROR, "The server's error message was: %s\n",
			   buf);
		return NULL;
	};

	return fp;
}

/* vim:set ts=4: */
