/* tlsComm.h - interface for the thin layer that looks
   sort of like fgets and fprintf, but might read or write
   to a socket or a TLS association 

   Neil Spring (nspring@cs.washington.edu)

   Comments in @'s are for lclint's benefit:
   http://lclint.cs.virginia.edu/
*/


/* opaque reference to the state associated with a 
   connection: may be just a file handle, or may include
   encryption state */
struct connection_state;

/* take a socket descriptor and negotiate a TLS connection
   over it */
/*@only@*/
struct connection_state *initialize_gnutls(int sd, /*@only@ */ char *name);

/* take a socket descriptor and bundle it into a connection
   state structure for later communication */
/*@only@*/
struct connection_state *initialize_unencrypted(int sd,	/*@only@ */
												char *name);

/* just like fprintf, only takes a connection state structure */
void tlscomm_printf(struct connection_state *scs, const char *format, ...);

/* modeled after fgets; may not work exactly the same */
int tlscomm_gets( /*@out@ */ char *buf,
				 int buflen, struct connection_state *scs);

/* gobbles lines until it finds one starting with {prefix},
   which is returned in buf */
int tlscomm_expect(struct connection_state *scs, const char *prefix,
				   /*@out@ */ char *buf,
				   int buflen);

/* terminates the TLS association or just closes the socket,
   and frees the connection state */
void tlscomm_close( /*@only@ */ struct connection_state *scs);