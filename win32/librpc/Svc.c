/**********************************************************************
 * ONC RPC for WIN32.
 * 1997 by WD Klotz
 * ESRF, BP 220, F-38640 Grenoble, CEDEX
 * klotz-tech@esrf.fr
 *
 * SUN's ONC RPC for Windows NT and Windows 95. Ammended port from
 * Martin F.Gergeleit's distribution. This version has been modified
 * and cleaned, such as to be compatible with Windows NT and Windows 95. 
 * Compiler: MSVC++ version 4.2 and 5.0.
 *
 * Users may use, copy or modify Sun RPC for the Windows NT Operating 
 * System according to the Sun copyright below.
 * RPC for the Windows NT Operating System COMES WITH ABSOLUTELY NO 
 * WARRANTY, NOR WILL I BE LIABLE FOR ANY DAMAGES INCURRED FROM THE 
 * USE OF. USE ENTIRELY AT YOUR OWN RISK!!!
 **********************************************************************/
/*********************************************************************
 * RPC for the Windows NT Operating System
 * 1993 by Martin F. Gergeleit
 * Users may use, copy or modify Sun RPC for the Windows NT Operating 
 * System according to the Sun copyright below.
 *
 * RPC for the Windows NT Operating System COMES WITH ABSOLUTELY NO 
 * WARRANTY, NOR WILL I BE LIABLE FOR ANY DAMAGES INCURRED FROM THE 
 * USE OF. USE ENTIRELY AT YOUR OWN RISK!!!
 *********************************************************************/

/* @(#)svc.c	2.4 88/08/11 4.0 RPCSRC; from 1.44 88/02/08 SMI */
/*
 * Sun RPC is a product of Sun Microsystems, Inc. and is provided for
 * unrestricted use provided that this legend is included on all tape
 * media and as a part of the software program in whole or part.  Users
 * may copy or modify Sun RPC without charge, but are not authorized
 * to license or distribute it to anyone else except as part of a product or
 * program developed by the user.
 * 
 * SUN RPC IS PROVIDED AS IS WITH NO WARRANTIES OF ANY KIND INCLUDING THE
 * WARRANTIES OF DESIGN, MERCHANTIBILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE, OR ARISING FROM A COURSE OF DEALING, USAGE OR TRADE PRACTICE.
 * 
 * Sun RPC is provided with no support and without any obligation on the
 * part of Sun Microsystems, Inc. to assist in its use, correction,
 * modification or enhancement.
 * 
 * SUN MICROSYSTEMS, INC. SHALL HAVE NO LIABILITY WITH RESPECT TO THE
 * INFRINGEMENT OF COPYRIGHTS, TRADE SECRETS OR ANY PATENTS BY SUN RPC
 * OR ANY PART THEREOF.
 * 
 * In no event will Sun Microsystems, Inc. be liable for any lost revenue
 * or profits or other special, indirect and consequential damages, even if
 * Sun has been advised of the possibility of such damages.
 * 
 * Sun Microsystems, Inc.
 * 2550 Garcia Avenue
 * Mountain View, California  94043
 */
#if !defined(lint) && defined(SCCSIDS)
static char sccsid[] = "@(#)svc.c 1.41 87/10/13 Copyr 1984 Sun Micro";
#endif

/*
 * svc.c, Server-side remote procedure call interface.
 *
 * There are two sets of procedures here.  The xprt routines are
 * for handling transport handles.  The svc routines handle the
 * list of service routines.
 *
 * Copyright (C) 1984, Sun Microsystems, Inc.
 */

#include "all_oncrpc.h"

#ifndef WIN32
extern int errno;
#endif

static SVCXPRT **xports;
#ifdef WIN32
static int sizeof_xports = FD_SETSIZE;
#endif


#define NULL_SVC ((struct svc_callout *)0)
#define	RQCRED_SIZE	400		/* this size is excessive */

/*
 * The services list
 * Each entry represents a set of procedures (an rpc program).
 * The dispatch routine takes request structs and runs the
 * apropriate procedure.
 */
static struct svc_callout {
	struct svc_callout *sc_next;
	u_long		    sc_prog;
	u_long		    sc_vers;
	void		    (*sc_dispatch)();
} *svc_head;

static struct svc_callout *svc_find();

/* ***************  SVCXPRT related stuff **************** */

/*
 * Activate a transport handle.
 */
void xprt_register(SVCXPRT *xprt)
{
	SOCKET sock = xprt->xp_sock;

#ifdef FD_SETSIZE
	if (xports == NULL) {
		xports = (SVCXPRT **)
			mem_alloc(FD_SETSIZE * sizeof(SVCXPRT *));
	}
#ifdef WIN32
	while (sock >= sizeof_xports) {
		SVCXPRT **old_xports;

		old_xports = xports;
		xports = (SVCXPRT **)
			mem_alloc(2 * sizeof_xports * sizeof(SVCXPRT *));
		bcopy((char*)old_xports, (char*)xports, sizeof_xports * sizeof(SVCXPRT *));
		sizeof_xports = 2 * sizeof_xports;
	}


	if (svc_fdset.fd_count < FD_SETSIZE) {
		xports[sock] = xprt;
		FD_SET(sock, &svc_fdset);
	} else {
		char str[256];
		
		sprintf_s(str, 256, "too many connections (%d), compilation constant FD_SETSIZE was only %d", sock, FD_SETSIZE);
		nt_rpc_report(str);
	}
#else
	if (sock < _rpc_dtablesize()) {
		xports[sock] = xprt;
		FD_SET(sock, &svc_fdset);
	}
#endif
#else
	if (sock < NOFILE) {
		xports[sock] = xprt;
		svc_fds |= (1 << sock);
	}
#endif /* def FD_SETSIZE */

}

/*
 * De-activate a transport handle. 
 */
void xprt_unregister(SVCXPRT *xprt)
{ 
	SOCKET sock = xprt->xp_sock;


#ifdef WIN32
	if ((xports[sock] == xprt)) {
		xports[sock] = (SVCXPRT *)0;
		FD_CLR((unsigned)sock, &svc_fdset);
#else
	if ((sock < _rpc_dtablesize()) && (xports[sock] == xprt)) {
		xports[sock] = (SVCXPRT *)0;
		FD_CLR(sock, &svc_fdset);
#endif
	}
}


/* ********************** CALLOUT list related stuff ************* */

/*
 * Add a service program to the callout list.
 * The dispatch routine will be called when a rpc request for this
 * program number comes in.
 */
bool_t svc_register(SVCXPRT *xprt, u_long prog,	u_long vers, void (*dispatch)(), int protocol)
{
	struct svc_callout *prev;
	struct svc_callout *s;

	if ((s = svc_find(prog, vers, &prev)) != NULL_SVC) {
		if (s->sc_dispatch == dispatch)
			goto pmap_it;  /* he is registering another xptr */
		return FALSE;
	}
	s = (struct svc_callout *)mem_alloc(sizeof(struct svc_callout));
	if (s == (struct svc_callout *)0) {
		return FALSE;
	}
	s->sc_prog = prog;
	s->sc_vers = vers;
	s->sc_dispatch = dispatch;
	s->sc_next = svc_head;
	svc_head = s;
pmap_it:
	/* now the information with the local binder service */
	if (protocol) {
		return (pmap_set(prog, vers, protocol, xprt->xp_port));
	}
	return TRUE;
}

/*
 * Remove a service program from the callout list.
 */
void svc_unregister(u_long prog, u_long vers)
{
	struct svc_callout *prev;
	struct svc_callout *s;

	if ((s = svc_find(prog, vers, &prev)) == NULL_SVC)
		return;
	if (prev == NULL_SVC) {
		svc_head = s->sc_next;
	} else {
		prev->sc_next = s->sc_next;
	}
	s->sc_next = NULL_SVC;
	mem_free((char *) s, (u_int) sizeof(struct svc_callout));
	/* now unregister the information with the local binder service */
	pmap_unset(prog, vers);
}

/*
 * Search the callout list for a program number, return the callout
 * struct.
 */
static struct svc_callout *svc_find(u_long prog, u_long vers,	struct svc_callout **prev)
{
	struct svc_callout *s, *p;

	p = NULL_SVC;
	for (s = svc_head; s != NULL_SVC; s = s->sc_next) {
		if ((s->sc_prog == prog) && (s->sc_vers == vers))
			goto done;
		p = s;
	}
done:
	*prev = p;
	return s;
}

/* ******************* REPLY GENERATION ROUTINES  ************ */

/*
 * Send a reply to an rpc request
 */
bool_t svc_sendreply(SVCXPRT *xprt, xdrproc_t xdr_results,	caddr_t xdr_location)
{
	struct rpc_msg rply; 

	rply.rm_direction = REPLY;  
	rply.rm_reply.rp_stat = MSG_ACCEPTED; 
	rply.acpted_rply.ar_verf = xprt->xp_verf; 
	rply.acpted_rply.ar_stat = SUCCESS;
	rply.acpted_rply.ar_results.where = xdr_location;
	rply.acpted_rply.ar_results.proc = xdr_results;
	return SVC_REPLY(xprt, &rply); 
}

/*
 * No procedure error reply
 */
void svcerr_noproc(SVCXPRT *xprt)
{
	struct rpc_msg rply;

	rply.rm_direction = REPLY;
	rply.rm_reply.rp_stat = MSG_ACCEPTED;
	rply.acpted_rply.ar_verf = xprt->xp_verf;
	rply.acpted_rply.ar_stat = PROC_UNAVAIL;
	SVC_REPLY(xprt, &rply);
}

/*
 * Can't decode args error reply
 */
void svcerr_decode(SVCXPRT *xprt)
{
	struct rpc_msg rply; 

	rply.rm_direction = REPLY; 
	rply.rm_reply.rp_stat = MSG_ACCEPTED; 
	rply.acpted_rply.ar_verf = xprt->xp_verf;
	rply.acpted_rply.ar_stat = GARBAGE_ARGS;
	SVC_REPLY(xprt, &rply); 
}

/*
 * Some system error
 */
void svcerr_systemerr(SVCXPRT *xprt)
{
	struct rpc_msg rply; 

	rply.rm_direction = REPLY; 
	rply.rm_reply.rp_stat = MSG_ACCEPTED; 
	rply.acpted_rply.ar_verf = xprt->xp_verf;
	rply.acpted_rply.ar_stat = SYSTEM_ERR;
	SVC_REPLY(xprt, &rply); 
}

/*
 * Authentication error reply
 */
void svcerr_auth(SVCXPRT *xprt,	enum auth_stat why)
{
	struct rpc_msg rply;

	rply.rm_direction = REPLY;
	rply.rm_reply.rp_stat = MSG_DENIED;
	rply.rjcted_rply.rj_stat = AUTH_ERROR;
	rply.rjcted_rply.rj_why = why;
	SVC_REPLY(xprt, &rply);
}

/*
 * Auth too weak error reply
 */
void svcerr_weakauth(SVCXPRT *xprt)
{
	svcerr_auth(xprt, AUTH_TOOWEAK);
}

/*
 * Program unavailable error reply
 */
void svcerr_noprog(SVCXPRT *xprt)
{
	struct rpc_msg rply;  

	rply.rm_direction = REPLY;   
	rply.rm_reply.rp_stat = MSG_ACCEPTED;  
	rply.acpted_rply.ar_verf = xprt->xp_verf;  
	rply.acpted_rply.ar_stat = PROG_UNAVAIL;
	SVC_REPLY(xprt, &rply);
}

/*
 * Program version mismatch error reply
 */
void svcerr_progvers(SVCXPRT *xprt, u_long low_vers,	u_long high_vers)
{
	struct rpc_msg rply;

	rply.rm_direction = REPLY;
	rply.rm_reply.rp_stat = MSG_ACCEPTED;
	rply.acpted_rply.ar_verf = xprt->xp_verf;
	rply.acpted_rply.ar_stat = PROG_MISMATCH;
	rply.acpted_rply.ar_vers.low = low_vers;
	rply.acpted_rply.ar_vers.high = high_vers;
	SVC_REPLY(xprt, &rply);
}

/* ******************* SERVER INPUT STUFF ******************* */

/*
 * Get server side input from some transport.
 *
 * Statement of authentication parameters management:
 * This function owns and manages all authentication parameters, specifically
 * the "raw" parameters (msg.rm_call.cb_cred and msg.rm_call.cb_verf) and
 * the "cooked" credentials (rqst->rq_clntcred).
 * However, this function does not know the structure of the cooked
 * credentials, so it make the following assumptions: 
 *   a) the structure is contiguous (no pointers), and
 *   b) the cred structure size does not exceed RQCRED_SIZE bytes. 
 * In all events, all three parameters are freed upon exit from this routine.
 * The storage is trivially management on the call stack in user land, but
 * is mallocated in kernel land.
 */

void svc_getreq(int rdfds)
{
#ifdef WIN32
int i;
#endif
	fd_set readfds;

	FD_ZERO(&readfds);

#ifdef WIN32
	i = 0;
	while (rdfds) {
		if (rdfds & 1)
			FD_SET(i, &readfds);
		rdfds = rdfds >> 1;
		i++;
	}
#else
	readfds.fds_bits[0] = rdfds;
#endif
	svc_getreqset(&readfds);
}

void svc_getreqset(fd_set *readfds)
{
	enum xprt_stat stat;
	struct rpc_msg msg;
	int prog_found;
	u_long low_vers;
	u_long high_vers;
	struct svc_req r;
	SVCXPRT *xprt;
#ifndef WIN32
	u_long mask;
	u_long *maskp;
	int setsize;
	int bit;
#endif
	SOCKET sock;
	char cred_area[2*MAX_AUTH_BYTES + RQCRED_SIZE];
	int i;

	msg.rm_call.cb_cred.oa_base = cred_area;
	msg.rm_call.cb_verf.oa_base = &(cred_area[MAX_AUTH_BYTES]);
	r.rq_clntcred = &(cred_area[2*MAX_AUTH_BYTES]);

#ifdef WIN32
	/* Loop through the sockets that have input ready */
	for ( i=0; (u_int)i<readfds->fd_count; i++ ) {
		sock = readfds->fd_array[i];
		/* sock has input waiting */
		xprt = xports[sock];
#else
	setsize = _rpc_dtablesize();	
	maskp = (u_long *)readfds->fds_bits;
	for (sock = 0; sock < setsize; sock += NFDBITS) {
	    for (mask = *maskp++; bit = ffs(mask); mask ^= (1 << (bit - 1))) {
		/* sock has input waiting */
		xprt = xports[sock + bit - 1];
#endif
		/* now receive msgs from xprtprt (support batch calls) */
		do {
			if (SVC_RECV(xprt, &msg)) {

				/* now find the exported program and call it */
				struct svc_callout *s;
				enum auth_stat why;

				r.rq_xprt = xprt;
				r.rq_prog = msg.rm_call.cb_prog;
				r.rq_vers = msg.rm_call.cb_vers;
				r.rq_proc = msg.rm_call.cb_proc;
				r.rq_cred = msg.rm_call.cb_cred;
				/* first authenticate the message */
				if ((why= _authenticate(&r, &msg)) != AUTH_OK) {
					svcerr_auth(xprt, why);
					goto call_done;
				}
				/* now match message with a registered service*/
				prog_found = FALSE;
				low_vers = 0;
				high_vers = 0;
				for (s = svc_head; s != NULL_SVC; s = s->sc_next) {
					if (s->sc_prog == r.rq_prog) {
						if (s->sc_vers == r.rq_vers) {
							(*s->sc_dispatch)(&r, xprt);
							goto call_done;
						}  /* found correct version */
						prog_found = TRUE;
						if (s->sc_vers <= low_vers)
							low_vers = s->sc_vers;
						if (s->sc_vers > high_vers)
							high_vers = s->sc_vers;
					}   /* found correct program */
				}
				/*
				 * if we got here, the program or version
				 * is not served ...
				 */
				if (prog_found)
					svcerr_progvers(xprt,
					low_vers, high_vers);
				else
					 svcerr_noprog(xprt);
				/* Fall through to ... */
			}
		call_done:
			if ((stat = SVC_STAT(xprt)) == XPRT_DIED){
				SVC_DESTROY(xprt);
				break;
			}
		} while (stat == XPRT_MOREREQS);
	}
}
