/* tlsComm.c - primitive routines to aid TLS communication
   within wmbiff, without rewriting each mailbox access
   scheme.  These functions hide whether the underlying
   transport is encrypted.

   Neil Spring (nspring@cs.washington.edu) */

/* TODO: handle "* BYE" internally? */

#include <stdarg.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#ifdef WITH_TLS
#include <gnutls.h>
#endif
#ifdef USE_DMALLOC
#include <dmalloc.h>
#endif

#include "tlsComm.h"

#include "Client.h"				/* debugging messages */

/* WARNING: implcitly uses scs to gain access to the mailbox
   that holds the per-mailbox debug flag. */
#define TDM(lvl, args...) DM(scs->pc, lvl, "comm: " ##args)

/* this is the per-connection state that is maintained for
   each connection; BIG variables are for ssl (null if not
   used). */
#define BUF_SIZE 1024
struct connection_state {
	int sd;
	char *name;
#ifdef WITH_TLS
	GNUTLS_STATE state;
	X509PKI_CLIENT_CREDENTIALS xcred;
#else
	/*@null@ */ void *state;
	/*@null@ */ void *xcred;
#endif
	char unprocessed[BUF_SIZE];
	Pop3 pc;					/* mailbox handle for debugging messages */
};

/* gotta do our own line buffering, sigh */
static int
getline_from_buffer(char *readbuffer, char *linebuffer, int linebuflen);
void handle_gnutls_read_error(int readbytes, struct connection_state *scs);

void tlscomm_close(struct connection_state *scs)
{
	TDM(DEBUG_INFO, "%s: closing.\n",
		(scs->name != NULL) ? scs->name : "null");

	/* not ok to call this more than once */
	if (scs->state) {
#ifdef WITH_TLS
		gnutls_bye(scs->state, GNUTLS_SHUT_RDWR);
		gnutls_x509pki_free_sc(scs->xcred);
		gnutls_deinit(scs->state);
		scs->xcred = NULL;
#endif
	} else {
		(void) close(scs->sd);
	}
	scs->sd = -1;
	scs->state = NULL;
	scs->xcred = NULL;
	free(scs->name);
	scs->name = NULL;
	free(scs);
}

/* this avoids blocking without using non-blocking i/o */
static int wait_for_it(int sd, int timeoutseconds)
{
	fd_set readfds;
	struct timeval tv;
	tv.tv_sec = timeoutseconds;
	tv.tv_usec = 0;
	FD_ZERO(&readfds);
	FD_SET(sd, &readfds);
	if (select(sd + 1, &readfds, NULL, NULL, &tv) == 0) {
		DMA(DEBUG_INFO,
			"select timed out after %d seconds on socket: %d\n",
			timeoutseconds, sd);
		return (0);
	}
	return (FD_ISSET(sd, &readfds));
}

static int
getline_from_buffer(char *readbuffer, char *linebuffer, int linebuflen)
{
	char *p, *q;
	int i;
	/* find end of line (stopping if linebuflen is too small. */
	for (p = readbuffer, i = 0;
		 *p != '\n' && *p != '\0' && i < linebuflen - 1; p++, i++);

	if (i != 0) {
		/* grab the end of line too! */
		i++;
		/* copy a line into the linebuffer */
		strncpy(linebuffer, readbuffer, (size_t) i);
		/* sigh, null terminate */
		linebuffer[i] = '\0';
		/* shift the rest over; this could be done
		   instead with strcpy... I think. */
		q = readbuffer;
		if (*p != '\0') {
			p++;
			do {
				*(q++) = *(p++);
			} while (*p != '\0');
		}
		/* null terminate */
		*(q++) = *(p++);
		/* return the length of the line */
	}
	return i;
}

/* eat lines, until one starting with prefix is found;
   this skips 'informational' IMAP responses */
/* the correct response to a return value of 0 is almost
   certainly tlscomm_close(scs): don't _expect() anything
   unless anything else would represent failure */
int tlscomm_expect(struct connection_state *scs,
				   const char *prefix, char *buf, int buflen)
{
	int prefixlen = (int) strlen(prefix);
	memset(buf, 0, buflen);
	TDM(DEBUG_INFO, "%s: expecting: %s\n", scs->name, prefix);
	while (wait_for_it(scs->sd, 10)) {
		int readbytes;
#ifdef WITH_TLS
		if (scs->state) {
			readbytes =
				gnutls_read(scs->state, scs->unprocessed, BUF_SIZE);
			if (readbytes < 0) {
				handle_gnutls_read_error(readbytes, scs);
				return 0;
			}
		} else
#endif
		{
			readbytes = read(scs->sd, scs->unprocessed, BUF_SIZE);
			if (readbytes < 0) {
				TDM(DEBUG_ERROR, "%s: error reading: %s\n", scs->name,
					strerror(errno));
				return 0;
			}
		}
		if (readbytes == 0) {
			return 0;			/* bummer */
		} else
			while (readbytes >= prefixlen) {
				int linebytes;
				linebytes =
					getline_from_buffer(scs->unprocessed, buf, buflen);
				if (linebytes == 0) {
					readbytes = 0;
				} else {
					readbytes -= linebytes;
					if (strncmp(buf, prefix, prefixlen) == 0) {
						TDM(DEBUG_INFO, "%s: got: %*s", scs->name,
							readbytes, buf);
						return 1;	/* got it! */
					}
					TDM(DEBUG_INFO, "%s: dumped(%d/%d): %.*s", scs->name,
						linebytes, readbytes, linebytes, buf);
				}
			}
	}
	TDM(DEBUG_ERROR, "%s: expecting: '%s', saw: %s", scs->name, prefix,
		buf);
	return 0;					/* wait_for_it failed */
}

int tlscomm_gets(char *buf, int buflen, struct connection_state *scs)
{
	return (tlscomm_expect(scs, "", buf, buflen));
}

void tlscomm_printf(struct connection_state *scs, const char *format, ...)
{
	va_list args;
	char buf[1024];
	int bytes;

	if (scs == NULL) {
		DMA(DEBUG_ERROR, "null connection to tlscomm_printf\n");
		abort();
	}
	va_start(args, format);
	bytes = vsnprintf(buf, 1024, format, args);
	va_end(args);

	if (scs->sd != -1) {
#ifdef WITH_TLS
		if (scs->state) {
			int written = gnutls_write(scs->state, buf, bytes);
			if (written < bytes) {
				TDM(DEBUG_ERROR,
					"Error %s prevented writing: %*s\n",
					gnutls_strerror(written), bytes, buf);
				return;
			}
		} else
#endif
			(void) write(scs->sd, buf, bytes);
	} else {
		printf
			("warning: tlscomm_printf called with an invalid socket descriptor\n");
		return;
	}
	TDM(DEBUG_INFO, "wrote %*s", bytes, buf);
}

/* most of this file only makes sense if using TLS. */
#ifdef WITH_TLS

/* taken from the GNUTLS documentation, version 0.3.0 and
   0.2.10; this may need to be updated from gnutls's cli.c
   (now common.h) if the gnutls interface changes, but that
   is only necessary if you want debug_comm. */
#define PRINTX(x,y) if (y[0]!=0) printf(" -   %s %s\n", x, y)
#define PRINT_DN(X) PRINTX( "CN:", X.common_name); \
	PRINTX( "OU:", X.organizational_unit_name); \
	PRINTX( "O:", X.organization); \
	PRINTX( "L:", X.locality_name); \
	PRINTX( "S:", X.state_or_province_name); \
	PRINTX( "C:", X.country); \
	PRINTX( "E:", X.email)
static int print_info(GNUTLS_STATE state)
{
	const char *tmp;
	CredType cred;
	gnutls_DN dn;
	const gnutls_datum *cert_list;
	CertificateStatus status;
	int cert_list_size = 0;

	tmp = gnutls_kx_get_name(gnutls_kx_get_algo(state));
	printf("- Key Exchange: %s\n", tmp);

	cred = gnutls_auth_get_type(state);
	switch (cred) {
	case GNUTLS_ANON:
		printf("- Anonymous DH using prime of %d bits\n",
			   gnutls_anon_client_get_dh_bits(state));
		break;
	case GNUTLS_X509PKI:
		cert_list =
			gnutls_x509pki_client_get_peer_certificate_list(state,
															&cert_list_size);
		status = gnutls_x509pki_client_get_peer_certificate_status(state);

		switch (status) {
		case GNUTLS_CERT_NOT_TRUSTED:
			printf("- Peer's X509 Certificate was NOT verified\n");
			break;
		case GNUTLS_CERT_EXPIRED:
			printf
				("- Peer's X509 Certificate was verified but is expired\n");
			break;
		case GNUTLS_CERT_TRUSTED:
			printf("- Peer's X509 Certificate was verified\n");
			break;
		case GNUTLS_CERT_NONE:
			printf("- Peer did not send any X509 Certificate.\n");
			break;
		case GNUTLS_CERT_INVALID:
			printf("- Peer's X509 Certificate was invalid\n");
			break;
		}

		if (cert_list_size > 0) {
			printf(" - Certificate info:\n");
			printf(" - Certificate version: #%d\n",
				   gnutls_x509pki_extract_certificate_version(&cert_list
															  [0]));

			gnutls_x509pki_extract_certificate_dn(&cert_list[0], &dn);
			PRINT_DN(dn);

			gnutls_x509pki_extract_certificate_issuer_dn(&cert_list[0],
														 &dn);
			printf(" - Certificate Issuer's info:\n");
			PRINT_DN(dn);
		}
    default:
      printf(" - Other.\n");
	}

	tmp = gnutls_protocol_get_name(gnutls_protocol_get_version(state));
	printf("- Version: %s\n", tmp);

	tmp = gnutls_compression_get_name(gnutls_compression_get_algo(state));
	printf("- Compression: %s\n", tmp);

	tmp = gnutls_cipher_get_name(gnutls_cipher_get_algo(state));
	printf("- Cipher: %s\n", tmp);

	tmp = gnutls_mac_get_name(gnutls_mac_get_algo(state));
	printf("- MAC: %s\n", tmp);

	return 0;
}

struct connection_state *initialize_gnutls(int sd, char *name, Pop3 pc)
{
	static int gnutls_initialized;
	int zok;
	struct connection_state *scs = malloc(sizeof(struct connection_state));

	scs->pc = pc;

	assert(sd >= 0);

	if (gnutls_initialized == 0) {
		assert(gnutls_global_init() == 0);
		gnutls_initialized = 1;
	}

	assert(gnutls_init(&scs->state, GNUTLS_CLIENT) == 0);
	{
		const int protocols[] = { GNUTLS_TLS1, GNUTLS_SSL3, 0 };
		const int ciphers[] =
			{ GNUTLS_CIPHER_3DES_CBC, GNUTLS_CIPHER_ARCFOUR, 0 };
		const int compress[] = { GNUTLS_COMP_ZLIB, GNUTLS_COMP_NULL, 0 };
		const int key_exch[] = { GNUTLS_KX_X509PKI_RSA, 0 };
		const int mac[] = { GNUTLS_MAC_SHA, GNUTLS_MAC_MD5, 0 };
		assert(gnutls_protocol_set_priority(scs->state, protocols) == 0);
		assert(gnutls_cipher_set_priority(scs->state, ciphers) == 0);
		assert(gnutls_compression_set_priority(scs->state, compress) == 0);
		assert(gnutls_kx_set_priority(scs->state, key_exch) == 0);
		assert(gnutls_mac_set_priority(scs->state, mac) == 0);
		/* no client private key */
		if (gnutls_x509pki_allocate_sc(&scs->xcred, 0) < 0) {
			DMA(DEBUG_ERROR, "gnutls memory error\n");
			exit(1);
		}
		gnutls_cred_set(scs->state, GNUTLS_X509PKI, scs->xcred);
		gnutls_transport_set_ptr(scs->state, sd);
		do {
			zok = gnutls_handshake(scs->state);
		} while (zok == GNUTLS_E_INTERRUPTED || zok == GNUTLS_E_AGAIN);
	}

	if (zok < 0) {
		TDM(DEBUG_ERROR, "%s: Handshake failed\n", name);
		TDM(DEBUG_ERROR, "%s: This may be a problem in gnutls, "
			"which is under development\n", name);
		TDM(DEBUG_ERROR,
			"%s: Specifically, problems have been found where the extnValue \n"
			"  buffer in _gnutls_get_ext_type() in lib/x509_extensions.c is too small in\n"
			"  gnutls versions up to 0.2.3.  This copy of wmbiff was compiled with \n"
			"  gnutls version %s.\n", name, LIBGNUTLS_VERSION);
		gnutls_perror(zok);
		gnutls_deinit(scs->state);
		free(scs);
		return (NULL);
	} else {
		TDM(DEBUG_INFO, "%s: Handshake was completed\n", name);
		if (scs->pc->debug >= DEBUG_INFO)
			print_info(scs->state);
		scs->sd = sd;
		scs->name = name;
	}
	return (scs);
}

/* moved down here, to keep from interrupting the flow with
   verbose error crap */
void handle_gnutls_read_error(int readbytes, struct connection_state *scs)
{
	if (gnutls_is_fatal_error(readbytes) == 1) {
		TDM(DEBUG_ERROR,
			"%s: Received corrupted data(%d) - server has terminated the connection abnormally\n",
			scs->name, readbytes);
	} else {
		if (readbytes == GNUTLS_E_WARNING_ALERT_RECEIVED
			|| readbytes == GNUTLS_E_FATAL_ALERT_RECEIVED)
			TDM(DEBUG_ERROR, "* Received alert [%d]\n",
				gnutls_alert_get_last(scs->state));
		if (readbytes == GNUTLS_E_REHANDSHAKE)
			TDM(DEBUG_ERROR, "* Received HelloRequest message\n");
	}
	TDM(DEBUG_ERROR,
		"%s: error reading: %s\n", scs->name, gnutls_strerror(readbytes));
}

#else
/* declare stubs when tls isn't compiled in */
struct connection_state *initialize_gnutls( /*@unused@ */ int sd,
										   /*@unused@ */ char *name)
{
	DMA(DEBUG_ERROR,
		"FATAL: tried to initialize ssl when ssl wasn't compiled in.\n");
	exit(EXIT_FAILURE);
}
#endif

/* either way: */
struct connection_state *initialize_unencrypted(int sd,
												/*@only@ */ char *name,
												Pop3 pc)
{
	struct connection_state *ret = malloc(sizeof(struct connection_state));
	assert(sd >= 0);
	assert(ret != NULL);
	ret->sd = sd;
	ret->name = name;
	ret->state = NULL;
	ret->xcred = NULL;
	ret->pc = pc;
	return (ret);
}

/* bad seed connections that can't be setup */
/*@only@*/
struct connection_state *initialize_blacklist( /*@only@ */ char *name)
{
	struct connection_state *ret = malloc(sizeof(struct connection_state));
	assert(ret != NULL);
	ret->sd = -1;
	ret->name = name;
	ret->state = NULL;
	ret->xcred = NULL;
	ret->pc = NULL;
	return (ret);
}


int tlscomm_is_blacklisted(const struct connection_state *scs)
{
	return (scs != NULL && scs->sd == -1);
}
