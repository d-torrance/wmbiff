/* $Id: charutil.h,v 1.10 2003/01/19 13:13:04 bluehal Exp $ */
/* Author: Mark Hurley  (debian4tux@telocity.com)
 *
 * Character / string manipulation utilities. 
 *
 */

#ifndef CHARUTIL
#define CHARUTIL

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_GNUREGEX_H
#include <gnuregex.h>
#else
#ifdef HAVE_REGEX_H
#include <regex.h>
#endif
#endif

void FullTrim(char *psValue);

void Bin2Hex(unsigned char *src, int length, char *dst);

void Encode_Base64(char *src, char *dst);
void Decode_Base64(char *src, char *dst);

/* helper function for the configuration line parser */
void copy_substring(char *destination,
					int startidx, int endidx, const char *source);

/* common to Pop3 and Imap4 authentication list grabber. */
void grab_authList(const char *source, char *destination);

#ifdef USE_GNU_REGEX
/* handles main regex work */
int compile_and_match_regex(const char *regex, const char *str,
							/*@out@ */ struct re_registers *regs);
#endif

/* acts like perl's function of the same name */
void chomp(char *s);

/* same as xstrdup, just better named ;) */
char *strdup_ordie(const char *c);
#endif
