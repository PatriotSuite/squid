
/*
 * $Id: icp.c,v 1.247 1997/05/08 23:23:11 wessels Exp $
 *
 * DEBUG: section 12    Client Handling
 * AUTHOR: Harvest Derived
 *
 * SQUID Internet Object Cache  http://squid.nlanr.net/Squid/
 * --------------------------------------------------------
 *
 *  Squid is the result of efforts by numerous individuals from the
 *  Internet community.  Development is led by Duane Wessels of the
 *  National Laboratory for Applied Network Research and funded by
 *  the National Science Foundation.
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
 *  
 */

/*
 * Copyright (c) 1994, 1995.  All rights reserved.
 *  
 *   The Harvest software was developed by the Internet Research Task
 *   Force Research Group on Resource Discovery (IRTF-RD):
 *  
 *         Mic Bowman of Transarc Corporation.
 *         Peter Danzig of the University of Southern California.
 *         Darren R. Hardy of the University of Colorado at Boulder.
 *         Udi Manber of the University of Arizona.
 *         Michael F. Schwartz of the University of Colorado at Boulder.
 *         Duane Wessels of the University of Colorado at Boulder.
 *  
 *   This copyright notice applies to software in the Harvest
 *   ``src/'' directory only.  Users should consult the individual
 *   copyright notices in the ``components/'' subdirectories for
 *   copyright information about other software bundled with the
 *   Harvest source code distribution.
 *  
 * TERMS OF USE
 *   
 *   The Harvest software may be used and re-distributed without
 *   charge, provided that the software origin and research team are
 *   cited in any use of the system.  Most commonly this is
 *   accomplished by including a link to the Harvest Home Page
 *   (http://harvest.cs.colorado.edu/) from the query page of any
 *   Broker you deploy, as well as in the query result pages.  These
 *   links are generated automatically by the standard Broker
 *   software distribution.
 *   
 *   The Harvest software is provided ``as is'', without express or
 *   implied warranty, and with no support nor obligation to assist
 *   in its use, correction, modification or enhancement.  We assume
 *   no liability with respect to the infringement of copyrights,
 *   trade secrets, or any patents, and are not responsible for
 *   consequential damages.  Proper use of the Harvest software is
 *   entirely the responsibility of the user.
 *  
 * DERIVATIVE WORKS
 *  
 *   Users may make derivative works from the Harvest software, subject 
 *   to the following constraints:
 *  
 *     - You must include the above copyright notice and these 
 *       accompanying paragraphs in all forms of derivative works, 
 *       and any documentation and other materials related to such 
 *       distribution and use acknowledge that the software was 
 *       developed at the above institutions.
 *  
 *     - You must notify IRTF-RD regarding your distribution of 
 *       the derivative work.
 *  
 *     - You must clearly notify users that your are distributing 
 *       a modified version and not the original Harvest software.
 *  
 *     - Any derivative product is also subject to these copyright 
 *       and use restrictions.
 *  
 *   Note that the Harvest software is NOT in the public domain.  We
 *   retain copyright, as specified above.
 *  
 * HISTORY OF FREE SOFTWARE STATUS
 *  
 *   Originally we required sites to license the software in cases
 *   where they were going to build commercial products/services
 *   around Harvest.  In June 1995 we changed this policy.  We now
 *   allow people to use the core Harvest software (the code found in
 *   the Harvest ``src/'' directory) for free.  We made this change
 *   in the interest of encouraging the widest possible deployment of
 *   the technology.  The Harvest software is really a reference
 *   implementation of a set of protocols and formats, some of which
 *   we intend to standardize.  We encourage commercial
 *   re-implementations of code complying to this set of standards.  
 */

#include "squid.h"

int neighbors_do_private_keys = 1;

char *log_tags[] =
{
    "NONE",
    "TCP_HIT",
    "TCP_MISS",
    "TCP_REFRESH_HIT",
    "TCP_REF_FAIL_HIT",
    "TCP_REFRESH_MISS",
    "TCP_CLIENT_REFRESH",
    "TCP_IMS_HIT",
    "TCP_IMS_MISS",
    "TCP_SWAPFAIL",
    "TCP_DENIED",
    "UDP_HIT",
    "UDP_HIT_OBJ",
    "UDP_MISS",
    "UDP_DENIED",
    "UDP_INVALID",
    "UDP_MISS_NOFETCH",
    "ERR_READ_TIMEOUT",
    "ERR_LIFETIME_EXP",
    "ERR_NO_CLIENTS_BIG_OBJ",
    "ERR_READ_ERROR",
    "ERR_CLIENT_ABORT",
    "ERR_CONNECT_FAIL",
    "ERR_INVALID_REQ",
    "ERR_UNSUP_REQ",
    "ERR_INVALID_URL",
    "ERR_NO_FDS",
    "ERR_DNS_FAIL",
    "ERR_NOT_IMPLEMENTED",
    "ERR_CANNOT_FETCH",
    "ERR_NO_RELAY",
    "ERR_DISK_IO",
    "ERR_ZERO_SIZE_OBJECT",
    "ERR_FTP_DISABLED",
    "ERR_PROXY_DENIED"
};

static icpUdpData *UdpQueueHead = NULL;
static icpUdpData *UdpQueueTail = NULL;

#define ICP_SENDMOREDATA_BUF SM_PAGE_SIZE

#define ICP_OP_ADD    0
#define ICP_OP_DEL    1

typedef struct icp_ctrl_t {
    clientHttpRequest *http;
    StoreEntry *entry;
    int fd;
    struct icp_ctrl_t *next;
} icp_ctrl_t;

/* Local functions */

static CWCB clientWriteComplete;
static CWCB icpHandleIMSComplete;
static PF clientReadRequest;
static PF connStateFree;
static STCB icpHandleAbort;
static STCB icpHandleStore;
static STCB icpGetHeadersForIMS;
static char *icpConstruct304reply _PARAMS((struct _http_reply *));
static int CheckQuickAbort2 _PARAMS((const clientHttpRequest *));
static int icpCheckTransferDone _PARAMS((clientHttpRequest *));
static int icpCheckUdpHit _PARAMS((StoreEntry *, request_t * request));
static int icpCheckUdpHitObj _PARAMS((StoreEntry * e, request_t * r, icp_common_t * h, int len));
static int icpProcessRequestControl _PARAMS((void *, int));
static void *icpCreateHitObjMessage _PARAMS((icp_opcode, int, const char *, int, int, StoreEntry *));
static void CheckQuickAbort _PARAMS((clientHttpRequest *));
static void checkFailureRatio _PARAMS((log_type, hier_code));
static void icpDetectNewRequest _PARAMS((int fd, void *));
static void icpHandleIcpV2 _PARAMS((int, struct sockaddr_in, char *, int));
static void icpHandleIcpV3 _PARAMS((int, struct sockaddr_in, char *, int));
static void icpLogIcp _PARAMS((icpUdpData *));
static void icpProcessMISS _PARAMS((int, clientHttpRequest *));
static void icpProcessRequestComplete _PARAMS((void *, int));

/*
 * This function is designed to serve a fairly specific purpose.
 * Occasionally our vBNS-connected caches can talk to each other, but not
 * the rest of the world.  Here we try to detect frequent failures which
 * make the cache unusable (e.g. DNS lookup and connect() failures).  If
 * the failure:success ratio goes above 1.0 then we go into "hit only"
 * mode where we only return UDP_HIT or UDP_MISS_NOFETCH.  Neighbors
 * will only fetch HITs from us if they are using the ICP protocol.  We
 * stay in this mode for 5 minutes.
 * 
 * Duane W., Sept 16, 1996
 */

#define FAILURE_MODE_TIME 300
static time_t hit_only_mode_until = 0;

static void
checkFailureRatio(log_type rcode, hier_code hcode)
{
    static double fail_ratio = 0.0;
    static double magic_factor = 100;
    double n_good;
    double n_bad;

    if (hcode == HIER_NONE)
	return;
    n_good = magic_factor / (1.0 + fail_ratio);
    n_bad = magic_factor - n_good;
    switch (rcode) {
    case ERR_DNS_FAIL:
    case ERR_CONNECT_FAIL:
    case ERR_READ_ERROR:
	n_bad++;
	break;
    default:
	n_good++;
    }
    fail_ratio = n_bad / n_good;
    if (hit_only_mode_until > squid_curtime)
	return;
    if (fail_ratio < 1.0)
	return;
    debug(12, 0, "Failure Ratio at %4.2f\n", fail_ratio);
    debug(12, 0, "Going into hit-only-mode for %d minutes...\n",
	FAILURE_MODE_TIME / 60);
    hit_only_mode_until = squid_curtime + FAILURE_MODE_TIME;
    fail_ratio = 0.8;		/* reset to something less than 1.0 */
}

static void
httpRequestFree(void *data)
{
    clientHttpRequest *http = data;
    clientHttpRequest **H;
    ConnStateData *conn = http->conn;
    int http_code = 0;
    int elapsed_msec;
    struct _hierarchyLogData *hierData = NULL;
    const char *content_type = NULL;
    if (http == NULL)
	fatal_dump("httpRequestFree: http == NULL");
    icpProcessRequestControl(http, ICP_OP_DEL);
    if (http->log_type > ERR_MAX)
	fatal_dump("httpRequestFree: http->log_type out of range.");
if (http->entry)
    debug(0,0,"httpRequestFree: '%s'\n", http->entry->url);
else
    debug(0,0,"httpRequestFree: %p\n", http);
    if (http->entry) {
	if (http->entry->mem_obj) {
	    http_code = http->entry->mem_obj->reply->code;
	    content_type = http->entry->mem_obj->reply->content_type;
	}
    } else {
	http_code = http->http_code;
    }
    elapsed_msec = tvSubMsec(http->start, current_time);
    if (http->request)
	hierData = &http->request->hierarchy;
    if (http->out.size || http->log_type) {
	HTTPCacheInfo->log_append(HTTPCacheInfo,
	    http->url,
	    conn->log_addr,
	    http->out.size,
	    log_tags[http->log_type],
	    RequestMethodStr[http->request->method],
	    http_code,
	    elapsed_msec,
	    conn->ident.ident,
	    hierData,
#if LOG_FULL_HEADERS
	    http->request_hdr,
	    http->reply_hdr,
#endif /* LOG_FULL_HEADERS */
	    content_type);
	HTTPCacheInfo->proto_count(HTTPCacheInfo,
	    http->request ? http->request->protocol : PROTO_NONE,
	    http->log_type);
	clientdbUpdate(conn->peer.sin_addr,
	    http->log_type,
	    ntohs(conn->me.sin_port));
    }
    if (http->redirect_state == REDIRECT_PENDING)
	redirectUnregister(http->url, http);
    if (http->acl_checklist) {
	debug(0, 0, "connStateFree: calling aclChecklistFree()\n");
	aclChecklistFree(http->acl_checklist);
    }
    checkFailureRatio(http->log_type,
	hierData ? hierData->code : HIER_NONE);
    safe_free(http->in.buf);
    meta_data.misc -= http->in.size;
    safe_free(http->url);
    safe_free(http->request_hdr);
#if LOG_FULL_HEADERS
    safe_free(http->reply_hdr);
#endif /* LOG_FULL_HEADERS */
    if (http->entry) {
	storeUnregister(http->entry, http);
	storeUnlockObject(http->entry);
	http->entry = NULL;
    }
    /* old_entry might still be set if we didn't yet get the reply
     * code in icpHandleIMSReply() */
    if (http->old_entry) {
	storeUnregister(http->old_entry, http);
	storeUnlockObject(http->old_entry);
	http->old_entry = NULL;
    }
    requestUnlink(http->request);
    H = &http->conn->chr;
    while (*H) {
	if (*H == NULL)
		fatal_dump("httpRequestFree: link list error");
	if (*H == http)
		break;	
	H = &(*H)->next;
    }
    *H = http->next;
    safe_free(http);
}

/* This is a handler normally called by comm_close() */
static void
connStateFree(int fd, void *data)
{
    ConnStateData *connState = data;
    clientHttpRequest *http;
    debug(0, 0, "connStateFree: FD %d\n", fd);
    if (connState == NULL)
	fatal_dump("connStateFree: connState == NULL");
    while ((http = connState->chr)) {
	connState->chr = http->next;
	httpRequestFree(http);
    }
    if (fd_table[fd].rwstate) {
	debug(0, 0, "connStateFree: calling commCancelWriteHandler()\n");
	commCancelWriteHandler(fd);
    }
    if (connState->ident.fd > -1)
	comm_close(connState->ident.fd);
    debug(0,0,"connStateFree: FD %d handled %d requests\n",
	fd, connState->nrequests);
    safe_free(connState);
}

void
icpParseRequestHeaders(clientHttpRequest * http)
{
    char *request_hdr = http->request_hdr;
    char *t = NULL;
    request_t *request = http->request;
    request->ims = -2;
    request->imslen = -1;
    if ((t = mime_get_header(request_hdr, "If-Modified-Since"))) {
	BIT_SET(request->flags, REQ_IMS);
	request->ims = parse_rfc1123(t);
	while ((t = strchr(t, ';'))) {
	    for (t++; isspace(*t); t++);
	    if (strncasecmp(t, "length=", 7) == 0)
		request->imslen = atoi(t + 7);
	}
    }
    if ((t = mime_get_header(request_hdr, "Pragma"))) {
	if (!strcasecmp(t, "no-cache"))
	    BIT_SET(request->flags, REQ_NOCACHE);
    }
    if (mime_get_header(request_hdr, "Authorization"))
	BIT_SET(request->flags, REQ_AUTH);
    if ((t = mime_get_header(request_hdr, "Proxy-Connection"))) {
	if (!strcasecmp(t, "Keep-Alive")) {
	    BIT_SET(request->flags, REQ_PROXY_KEEPALIVE);
	    debug(0, 0, "Got: %s\n", t);
	}
    }
    if ((t = mime_get_header(request_hdr, "Via")))
	if (strstr(t, ThisCache)) {
	    if (!http->accel) {
		debug(12, 1, "WARNING: Forwarding loop detected for '%s'\n",
		    http->url);
		debug(12, 1, "--> %s\n", t);
	    }
	    BIT_SET(request->flags, REQ_LOOPDETECT);
	}
#if USE_USERAGENT_LOG
    if ((t = mime_get_header(request_hdr, "User-Agent")))
	logUserAgent(fqdnFromAddr(http->conn->peer.sin_addr), t);
#endif
    request->max_age = -1;
    if ((t = mime_get_header(request_hdr, "Cache-control"))) {
	if (!strncasecmp(t, "Max-age=", 8))
	    request->max_age = atoi(t + 8);
    }
    if (request->method == METHOD_TRACE) {
	if ((t = mime_get_header(request_hdr, "Max-Forwards")))
	    request->max_forwards = atoi(t);
    }
}

static int
icpCachable(clientHttpRequest * http)
{
    const char *request = http->url;
    request_t *req = http->request;
    method_t method = req->method;
    const wordlist *p;
    if (BIT_TEST(http->request->flags, REQ_AUTH))
	return 0;
    for (p = Config.cache_stoplist; p; p = p->next) {
	if (strstr(request, p->key))
	    return 0;
    }
    if (Config.cache_stop_relist)
	if (aclMatchRegex(Config.cache_stop_relist, request))
	    return 0;
    if (req->protocol == PROTO_HTTP)
	return httpCachable(method);
    /* FTP is always cachable */
    if (req->protocol == PROTO_GOPHER)
	return gopherCachable(request);
    if (req->protocol == PROTO_WAIS)
	return 0;
    if (method == METHOD_CONNECT)
	return 0;
    if (method == METHOD_TRACE)
	return 0;
    if (req->protocol == PROTO_CACHEOBJ)
	return 0;
    return 1;
}

/* Return true if we can query our neighbors for this object */
static int
icpHierarchical(clientHttpRequest * http)
{
    const char *url = http->url;
    request_t *request = http->request;
    method_t method = request->method;
    const wordlist *p = NULL;

    /* IMS needs a private key, so we can use the hierarchy for IMS only
     * if our neighbors support private keys */
    if (BIT_TEST(request->flags, REQ_IMS) && !neighbors_do_private_keys)
	return 0;
    if (BIT_TEST(request->flags, REQ_AUTH))
	return 0;
    if (method == METHOD_TRACE)
	return 1;
    if (method != METHOD_GET)
	return 0;
    /* scan hierarchy_stoplist */
    for (p = Config.hierarchy_stoplist; p; p = p->next)
	if (strstr(url, p->key))
	    return 0;
    if (BIT_TEST(request->flags, REQ_LOOPDETECT))
	return 0;
    if (request->protocol == PROTO_HTTP)
	return httpCachable(method);
    if (request->protocol == PROTO_GOPHER)
	return gopherCachable(url);
    if (request->protocol == PROTO_WAIS)
	return 0;
    if (request->protocol == PROTO_CACHEOBJ)
	return 0;
    return 1;
}

void
icpSendERRORComplete(int fd, char *buf, int size, int errflag, void *data)
{
    clientHttpRequest *http = data;
    debug(12, 4, "icpSendERRORComplete: FD %d, %d bytes\n", fd, size);
    http->out.size += size;
    comm_close(fd);
}

/* Send ERROR message. */
void
icpSendERROR(int fd,
    log_type errorCode,
    const char *text,
    clientHttpRequest * http,
    int httpCode)
{
    int buf_len = 0;
    char *buf = NULL;
    http->log_type = errorCode;
    http->http_code = httpCode;
    if (http->entry && http->entry->mem_obj) {
	if (http->out.size > 0) {
	    comm_close(fd);
	    return;
	}
    }
    if (text == NULL) {
	comm_close(fd);
	return;
    }
    buf_len = strlen(text);
    buf_len = buf_len > 4095 ? 4095 : buf_len;
    buf = get_free_4k_page();
    xstrncpy(buf, text, 4096);
    comm_write(fd,
	buf,
	buf_len,
	icpSendERRORComplete,
	http,
	put_free_4k_page);
}

#if LOG_FULL_HEADERS
/*
 * Scan beginning of swap object being returned and hope that it contains
 * a complete MIME header for use in reply header logging (log_append).
 */
void
icp_maybe_remember_reply_hdr(clientHttpRequest * http)
{
    char *mime;
    char *end;

    if (connState && http->entry && http->entry->mem_obj
	&& http->entry->mem_obj->data && http->entry->mem_obj->data->head
	&& (mime = http->entry->mem_obj->data->head->data) != NULL
	&& (end = mime_headers_end(mime)) != NULL) {
	int mime_len = end - mime;
	char *buf = xcalloc(mime_len + 1, 1);

	xstrncpy(buf, mime, mime_len);
	http->reply_hdr = buf;
	debug(12, 5, "icp_maybe_remember_reply_hdr: ->\n%s<-\n", buf);
    } else {
	http->reply_hdr = 0;
    }
}

#endif /* LOG_FULL_HEADERS */
/* Send available data from an object in the cache.  This is called either
 * on select for  write or directly by icpHandleStore. */

int
icpSendMoreData(int fd, clientHttpRequest * http)
{
    StoreEntry *entry = http->entry;
    int len;
    char *buf = NULL;
    char *p = NULL;
    int x;

    debug(12, 5, "icpSendMoreData: '%s' sz %d: len %d: off %d.\n",
	entry->url,
	entry->object_len,
	entry->mem_obj ? entry->mem_obj->e_current_len : 0,
	http->out.offset);
    buf = get_free_4k_page();
    x = storeClientCopy(http->entry,
	http->out.offset,
	ICP_SENDMOREDATA_BUF,
	buf,
	&len,
	http);
    if (x < 0) {
	debug(12, 1, "storeClientCopy returned %d for '%s'\n", x, entry->key);
	put_free_4k_page(buf);
	comm_close(fd);
	return COMM_ERROR;
    }
#if LOG_FULL_HEADERS
    if (http->out.offset == 0 && len > 0)
	icp_maybe_remember_reply_hdr(http);
#endif /* LOG_FULL_HEADERS */
    http->out.offset += len;
    if (http->request->method == METHOD_HEAD) {
	if ((p = mime_headers_end(buf))) {
	    *p = '\0';
	    len = p - buf;
	    /* force end */
	    http->out.offset = entry->mem_obj->e_current_len;
	}
    }
    comm_write(fd,
	buf,
	len,
	clientWriteComplete,
	http,
	put_free_4k_page);
    return COMM_OK;
}

/* Called by storage manager when more data arrives from source. 
 * Starts state machine towards client with new batch of data or
 * error messages.  We get here by invoking the handlers in the
 * pending list.
 */
static void
icpHandleStore(void *data)
{
    clientHttpRequest *http = data;
    StoreEntry *entry = http->entry;
    int fd = http->conn->fd;
    debug(12, 5, "icpHandleStore: FD %d '%s'\n", fd, entry->url);
    if (entry->store_status == STORE_ABORTED) {
	debug(12, 3, "icpHandleStore: abort_code=%d url='%s'\n",
	    entry->mem_obj->abort_code, entry->url);
	icpSendERROR(fd,
	    entry->mem_obj->abort_code,
	    entry->mem_obj->e_abort_msg,
	    http,
	    400);
	return;
    }
    if (http->entry != entry)
	fatal_dump("icpHandleStore: entry mismatch!");
    icpSendMoreData(fd, http);
}

static void
clientWriteComplete(int fd, char *buf, int size, int errflag, void *data)
{
    clientHttpRequest *http = data;
    ConnStateData *conn;
    StoreEntry *entry = http->entry;
    http->out.size += size;
    debug(12, 5, "clientWriteComplete: FD %d, sz %d, err %d, off %d, len %d\n",
	fd, size, errflag, http->out.offset, entry->object_len);
    if (errflag) {
	CheckQuickAbort(http);
	/* Log the number of bytes that we managed to read */
	HTTPCacheInfo->proto_touchobject(HTTPCacheInfo,
	    urlParseProtocol(entry->url),
	    http->out.size);
	comm_close(fd);
    } else if (http->out.offset < entry->mem_obj->e_current_len) {
	/* More data available locally; write it now */
	icpSendMoreData(fd, http);
    } else if (icpCheckTransferDone(http)) {
	/* We're finished case */
	HTTPCacheInfo->proto_touchobject(HTTPCacheInfo,
	    http->request->protocol,
	    http->out.size);
	if (http->entry->mem_obj->reply->content_length <= 0) {
debug(0,0,"clientWriteComplete: closing FD %d; no content-length\n", fd);
	    comm_close(fd);
	} else if (BIT_TEST(http->request->flags, REQ_PROXY_KEEPALIVE)) {
	    conn = http->conn;
	    httpRequestFree(http);
	    commSetSelect(fd, COMM_SELECT_READ, NULL, NULL, 0);
	    icpDetectNewRequest(fd, conn);
	} else {
	    comm_close(fd);
	}
    } else {
	/* More data will be coming from primary server; register with 
	 * storage manager. */
	storeRegister(http->entry, fd, icpHandleStore, http);
    }
}

static void
icpGetHeadersForIMS(void *data)
{
    clientHttpRequest *http = data;
    int fd = http->conn->fd;
    StoreEntry *entry = http->entry;
    MemObject *mem = entry->mem_obj;
    char *reply = NULL;

    if (mem->reply->code == 0) {
	if (entry->mem_status == IN_MEMORY) {
	    icpProcessMISS(fd, http);
	    return;
	}
	/* All headers are not yet available, wait for more data */
	storeRegister(entry, fd, icpGetHeadersForIMS, http);
	return;
    }
    /* All headers are available, check if object is modified or not */
    /* ---------------------------------------------------------------
     * Removed check for reply->code != 200 because of a potential
     * problem with ICP.  We will return a HIT for any public, cached
     * object.  This includes other responses like 301, 410, as coded in
     * http.c.  It is Bad(tm) to return UDP_HIT and then, if the reply
     * code is not 200, hand off to icpProcessMISS(), which may disallow
     * the request based on 'miss_access' rules.  Alternatively, we might
     * consider requiring returning UDP_HIT only for 200's.  This
     * problably means an entry->flag bit, which would be lost during
     * restart because the flags aren't preserved across restarts.
     * --DW 3/11/96.
     * ---------------------------------------------------------------- */
#ifdef CHECK_REPLY_CODE_NOTEQUAL_200
    /* Only objects with statuscode==200 can be "Not modified" */
    if (mem->reply->code != 200) {
	debug(12, 4, "icpGetHeadersForIMS: Reply code %d!=200\n",
	    mem->reply->code);
	icpProcessMISS(fd, http);
	return;
    }
    +
#endif
	http->log_type = LOG_TCP_IMS_HIT;
    entry->refcount++;
    if (modifiedSince(entry, http->request)) {
	icpSendMoreData(fd, http);
	return;
    }
    debug(12, 4, "icpGetHeadersForIMS: Not modified '%s'\n", entry->url);
    reply = icpConstruct304reply(mem->reply);
    comm_write(fd,
	xstrdup(reply),
	strlen(reply),
	icpHandleIMSComplete,
	http,
	xfree);
}

static void
icpHandleIMSComplete(int fd, char *buf_unused, int size, int errflag, void *data)
{
    clientHttpRequest *http = data;
    StoreEntry *entry = http->entry;
    debug(12, 5, "icpHandleIMSComplete: Not Modified sent '%s'\n", entry->url);
    HTTPCacheInfo->proto_touchobject(HTTPCacheInfo,
	http->request->protocol,
	size);
    /* Set up everything for the logging */
    storeUnregister(entry, http);
    storeUnlockObject(entry);
    http->entry = NULL;
    http->out.size += size;
    http->http_code = 304;
    comm_close(fd);
}

/*
 * Below, we check whether the object is a hit or a miss.  If it's a hit,
 * we check whether the object is still valid or whether it is a MISS_TTL.
 */
void
icpProcessRequest(int fd, clientHttpRequest * http)
{
    icp_ctrl_t *ctrlp;
    char *url = http->url;
    const char *pubkey = NULL;
    StoreEntry *entry = NULL;
    request_t *request = http->request;
    char *reply;
    debug(12, 4, "icpProcessRequest: %s '%s'\n",
	RequestMethodStr[http->request->method],
	url);
    if (http->request->method == METHOD_CONNECT) {
	http->log_type = LOG_TCP_MISS;
	sslStart(fd,
	    url,
	    http->request,
	    http->request_hdr,
	    &http->out.size);
	return;
    } else if (request->method == METHOD_PURGE) {
	clientPurgeRequest(http);
	return;
    } else if (request->method == METHOD_TRACE) {
	if (request->max_forwards == 0) {
	    reply = clientConstructTraceEcho(http);
	    comm_write(fd,
		xstrdup(reply),
		strlen(reply),
		icpSendERRORComplete,
		http,
		xfree);
	    return;
	}
	/* yes, continue */
    } else if (request->method != METHOD_GET) {
	http->log_type = LOG_TCP_MISS;
	passStart(fd,
	    url,
	    http->request,
	    http->request_hdr,
	    http->req_hdr_sz,
	    &http->out.size);
	return;
    }
    if (icpCachable(http))
	BIT_SET(request->flags, REQ_CACHABLE);
    if (icpHierarchical(http))
	BIT_SET(request->flags, REQ_HIERARCHICAL);
    debug(12, 5, "icpProcessRequest: REQ_NOCACHE = %s\n",
	BIT_TEST(request->flags, REQ_NOCACHE) ? "SET" : "NOT SET");
    debug(12, 5, "icpProcessRequest: REQ_CACHABLE = %s\n",
	BIT_TEST(request->flags, REQ_CACHABLE) ? "SET" : "NOT SET");
    debug(12, 5, "icpProcessRequest: REQ_HIERARCHICAL = %s\n",
	BIT_TEST(request->flags, REQ_HIERARCHICAL) ? "SET" : "NOT SET");

    /* NOTE on HEAD requests: We currently don't cache HEAD reqeusts
     * at all, so look for the corresponding GET object, or just go
     * directly. The only way to get a TCP_HIT on a HEAD reqeust is
     * if someone already did a GET.  Maybe we should turn HEAD
     * misses into full GET's?  */
    if (http->request->method == METHOD_HEAD) {
	pubkey = storeGeneratePublicKey(http->url, METHOD_GET);
    } else
	pubkey = storeGeneratePublicKey(http->url, http->request->method);

    if ((entry = storeGet(pubkey)) == NULL) {
	/* this object isn't in the cache */
	http->log_type = LOG_TCP_MISS;
    } else if (!storeEntryValidToSend(entry)) {
	http->log_type = LOG_TCP_MISS;
	storeRelease(entry);
	entry = NULL;
    } else if (BIT_TEST(request->flags, REQ_NOCACHE)) {
	/* IMS+NOCACHE should not eject valid object */
	if (!BIT_TEST(request->flags, REQ_IMS))
	    storeRelease(entry);
	/* NOCACHE should always eject negative cached object */
	else if (BIT_TEST(entry->flag, ENTRY_NEGCACHED))
	    storeRelease(entry);
	ipcacheReleaseInvalid(http->request->host);
	entry = NULL;
	http->log_type = LOG_TCP_CLIENT_REFRESH;
    } else if (refreshCheck(entry, request, 0)) {
	/* The object is in the cache, but it needs to be validated.  Use
	 * LOG_TCP_REFRESH_MISS for the time being, maybe change it to
	 * _HIT later in icpHandleIMSReply() */
	if (request->protocol == PROTO_HTTP)
	    http->log_type = LOG_TCP_REFRESH_MISS;
	else
	    http->log_type = LOG_TCP_MISS;	/* XXX zoinks */
    } else if (BIT_TEST(request->flags, REQ_IMS)) {
	/* User-initiated IMS request for something we think is valid */
	http->log_type = LOG_TCP_IMS_MISS;
    } else {
	http->log_type = LOG_TCP_HIT;
    }

    ctrlp = xmalloc(sizeof(icp_ctrl_t));
    ctrlp->http = http;
    ctrlp->entry = entry;
    ctrlp->fd = fd;
    icpProcessRequestControl(ctrlp, ICP_OP_ADD);

    debug(12, 4, "icpProcessRequest: %s for '%s'\n",
	log_tags[http->log_type],
	http->url);

    if (entry)
	storeLockObject(entry, icpProcessRequestComplete, ctrlp);
    else
	icpProcessRequestComplete(ctrlp, 0);
}


/* We have to maintain a state of what's outstanding because a race
 * condition occurs when a request comes to open a file and the abort
 * request comes in before the file is openned.  In this case, the
 * clientHttpRequest will be freed.  That is not wholly bad, what is bad
 * is when another bit of code grabs the space malloc'ed for the
 * clientHttpRequest and scribbles in it, then we're in trouble! */

static int
icpProcessRequestControl(void *data, int operation)
{
    static icp_ctrl_t *list = NULL;
    icp_ctrl_t *curr, *prev;
    icp_ctrl_t *ctrlp;
    if (operation == ICP_OP_ADD) {
	ctrlp = data;
	ctrlp->next = list;
	list = ctrlp;
	return 1;
    } else if (operation == ICP_OP_DEL) {
	prev = NULL;
	for (curr = list; curr != NULL; prev = curr, curr = curr->next)
	    if (curr->http == data)
		break;
	if (curr == NULL)
	    return 0;
	if (prev == NULL)
	    list = curr->next;
	else
	    prev->next = curr->next;
	return 1;
    }
    fatal_dump("icpProcessRequestControl: bad operation");
    return 0;
}


static void
icpProcessRequestComplete(void *data, int status)
{
    icp_ctrl_t *ctrlp = data;
    clientHttpRequest *http = ctrlp->http;
    StoreEntry *entry = ctrlp->entry;
    int fd = ctrlp->fd;
    debug(12, 3, "icpProcessRequestComplete: '%s'\n", http->url);
    if (icpProcessRequestControl(http, ICP_OP_DEL) == 0) {
	if (entry) {
	    if (status < 0)
		entry->lock_count++;
	    else
		file_close(entry->mem_obj->swapin_fd);
	}
	safe_free(ctrlp);
	return;
    }
    safe_free(ctrlp);
    /* The following status < 0 check grabs an UGLY race condition.  If
     * an operation aborts while the open is not complete AND file_open
     * failed then storeLockObjectComplete would have decremented the
     * lock count when it shouldn't have.  It will have already been
     * done by httpRequestFree. */
    if (entry && status < 0) {
	storeRelease(entry);
	entry = NULL;
	http->log_type = LOG_TCP_SWAPIN_FAIL;
    }
    if (entry)
	storeClientListAdd(entry, http, 0);
    http->entry = entry;	/* Save a reference to the object */
    http->out.offset = 0;
    switch (http->log_type) {
    case LOG_TCP_HIT:
	entry->refcount++;	/* HIT CASE */
	icpSendMoreData(fd, http);
	break;
    case LOG_TCP_IMS_MISS:
	icpGetHeadersForIMS(http);
	break;
    case LOG_TCP_REFRESH_MISS:
	icpProcessExpired(fd, http);
	break;
    default:
	icpProcessMISS(fd, http);
	break;
    }
}

/*
 * Prepare to fetch the object as it's a cache miss of some kind.
 */
static void
icpProcessMISS(int fd, clientHttpRequest * http)
{
    char *url = http->url;
    char *request_hdr = http->request_hdr;
    StoreEntry *entry = NULL;
    aclCheck_t ch;
    int answer;
    char *buf;

    debug(12, 4, "icpProcessMISS: '%s %s'\n",
	RequestMethodStr[http->request->method], url);
    debug(12, 10, "icpProcessMISS: request_hdr:\n%s\n", request_hdr);

    /* Check if this host is allowed to fetch MISSES from us */
    memset(&ch, '\0', sizeof(aclCheck_t));
    ch.src_addr = http->conn->peer.sin_addr;
    ch.request = http->request;
    answer = aclCheckFast(Config.accessList.MISS, &ch);
    if (answer == 0) {
	http->http_code = 400;
	buf = access_denied_msg(http->http_code,
	    http->request->method,
	    http->url,
	    fd_table[fd].ipaddr);
	icpSendERROR(fd, LOG_TCP_DENIED, buf, http, http->http_code);
	return;
    }
    /* Get rid of any references to a StoreEntry (if any) */
    if (http->entry) {
	storeUnregister(http->entry, http);
	storeUnlockObject(http->entry);
	http->entry = NULL;
    }
    entry = storeCreateEntry(url,
	request_hdr,
	http->req_hdr_sz,
	http->request->flags,
	http->request->method);
    /* NOTE, don't call storeLockObject(), storeCreateEntry() does it */
    storeClientListAdd(entry, http, 0);
    entry->mem_obj->fd = fd;

    entry->refcount++;		/* MISS CASE */
    http->entry = entry;
    http->out.offset = 0;
    /* Register with storage manager to receive updates when data comes in. */
    storeRegister(entry, fd, icpHandleStore, http);
    protoDispatch(fd, http->entry, http->request);
    return;
}

static void
icpLogIcp(icpUdpData * queue)
{
    icp_common_t *header = (icp_common_t *) (void *) queue->msg;
    char *url = (char *) header + sizeof(icp_common_t);

    ICPCacheInfo->proto_touchobject(ICPCacheInfo,
	queue->proto,
	queue->len);
    ICPCacheInfo->proto_count(ICPCacheInfo,
	queue->proto,
	queue->logcode);
    clientdbUpdate(queue->address.sin_addr,
	queue->logcode,
	Config.Port.icp);
    if (!Config.Options.log_udp)
	return;
    HTTPCacheInfo->log_append(HTTPCacheInfo,
	url,
	queue->address.sin_addr,
	queue->len,
	log_tags[queue->logcode],
	IcpOpcodeStr[ICP_OP_QUERY],
	0,
#ifndef LESS_TIMING
	tvSubMsec(queue->start, current_time),
#else
	0,
#endif
	NULL,			/* ident */
	NULL,			/* hierarchy data */
#if LOG_FULL_HEADERS
	NULL,			/* request header */
	NULL,			/* reply header */
#endif /* LOG_FULL_HEADERS */
	NULL);			/* content-type */
}

void
icpUdpReply(int fd, void *data)
{
    icpUdpData *queue = data;
    int x;

    /* Disable handler, in case of errors. */
    commSetSelect(fd,
	COMM_SELECT_WRITE,
	NULL,
	NULL, 0);
    while ((queue = UdpQueueHead)) {
	debug(12, 5, "icpUdpReply: FD %d sending %d bytes to %s port %d\n",
	    fd,
	    queue->len,
	    inet_ntoa(queue->address.sin_addr),
	    ntohs(queue->address.sin_port));
	x = comm_udp_sendto(fd,
	    &queue->address,
	    sizeof(struct sockaddr_in),
	    queue->msg,
	    queue->len);

	if (x < 0) {
	    if (errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR)
		break;		/* don't de-queue */
	}
	UdpQueueHead = queue->next;
	if (queue->logcode)
	    icpLogIcp(queue);
	safe_free(queue->msg);
	safe_free(queue);
    }
    /* Reinstate handler if needed */
    if (UdpQueueHead) {
	commSetSelect(fd,
	    COMM_SELECT_WRITE,
	    icpUdpReply,
	    UdpQueueHead, 0);
    }
}

void *
icpCreateMessage(
    icp_opcode opcode,
    int flags,
    const char *url,
    int reqnum,
    int pad)
{
    char *buf = NULL;
    icp_common_t *headerp = NULL;
    char *urloffset = NULL;
    int buf_len;

    buf_len = sizeof(icp_common_t) + strlen(url) + 1;
    if (opcode == ICP_OP_QUERY)
	buf_len += sizeof(u_num32);
    buf = xcalloc(buf_len, 1);
    headerp = (icp_common_t *) (void *) buf;
    headerp->opcode = opcode;
    headerp->version = ICP_VERSION_CURRENT;
    headerp->length = htons(buf_len);
    headerp->reqnum = htonl(reqnum);
    headerp->flags = htonl(flags);
    headerp->pad = htonl(pad);
    headerp->shostid = htonl(theOutICPAddr.s_addr);
    urloffset = buf + sizeof(icp_common_t);
    if (opcode == ICP_OP_QUERY)
	urloffset += sizeof(u_num32);
    xmemcpy(urloffset, url, strlen(url));
    return buf;
}

static void *
icpCreateHitObjMessage(
    icp_opcode opcode,
    int flags,
    const char *url,
    int reqnum,
    int pad,
    StoreEntry * entry)
{
    char *buf = NULL;
    char *entryoffset = NULL;
    char *urloffset = NULL;
    icp_common_t *headerp = NULL;
    int buf_len;
    u_short data_sz;
    int size;
    MemObject *m = entry->mem_obj;

    buf_len = sizeof(icp_common_t) + strlen(url) + 1 + 2 + entry->object_len;
    if (opcode == ICP_OP_QUERY)
	buf_len += sizeof(u_num32);
    buf = xcalloc(buf_len, 1);
    headerp = (icp_common_t *) (void *) buf;
    headerp->opcode = opcode;
    headerp->version = ICP_VERSION_CURRENT;
    headerp->length = htons(buf_len);
    headerp->reqnum = htonl(reqnum);
    headerp->flags = htonl(flags);
    headerp->pad = htonl(pad);
    headerp->shostid = htonl(theOutICPAddr.s_addr);
    urloffset = buf + sizeof(icp_common_t);
    xmemcpy(urloffset, url, strlen(url));
    data_sz = htons((u_short) entry->object_len);
    entryoffset = urloffset + strlen(url) + 1;
    xmemcpy(entryoffset, &data_sz, sizeof(u_short));
    entryoffset += sizeof(u_short);
    size = m->data->mem_copy(m->data, 0, entryoffset, entry->object_len);
    if (size < 0 || size != entry->object_len) {
	debug(12, 1, "icpCreateHitObjMessage: copy failed, wanted %d got %d bytes\n",
	    entry->object_len, size);
	safe_free(buf);
	return NULL;
    }
    return buf;
}

void
icpUdpSend(int fd,
    const struct sockaddr_in *to,
    icp_common_t * msg,
    log_type logcode,
    protocol_t proto)
{
    icpUdpData *data = xcalloc(1, sizeof(icpUdpData));

    debug(12, 4, "icpUdpSend: Queueing %s for %s\n",
	IcpOpcodeStr[msg->opcode],
	inet_ntoa(to->sin_addr));
    data->address = *to;
    data->msg = msg;
    data->len = (int) ntohs(msg->length);
#ifndef LESS_TIMING
    data->start = current_time;	/* wrong for HIT_OBJ */
#endif
    data->logcode = logcode;
    data->proto = proto;
    AppendUdp(data);
    commSetSelect(fd,
	COMM_SELECT_WRITE,
	icpUdpReply,
	UdpQueueHead, 0);
}

static int
icpCheckUdpHit(StoreEntry * e, request_t * request)
{
    if (e == NULL)
	return 0;
    if (!storeEntryValidToSend(e))
	return 0;
    if (refreshCheck(e, request, 30))
	return 0;
    /* MUST NOT do UDP_HIT_OBJ if object is not in memory with async_io. The */
    /* icpHandleV2 code has not been written to support it - squid will die! */
#if USE_ASYNC_IO || defined(MEM_UDP_HIT_OBJ)
    if (e->mem_status != IN_MEMORY)
	return 0;
#endif
    return 1;
}

static int
icpCheckUdpHitObj(StoreEntry * e, request_t * r, icp_common_t * h, int len)
{
    if (!BIT_TEST(h->flags, ICP_FLAG_HIT_OBJ))	/* not requested */
	return 0;
    if (len > Config.udpMaxHitObjsz)	/* too big */
	return 0;
    if (refreshCheck(e, r, 0))	/* stale */
	return 0;
#ifdef MEM_UDP_HIT_OBJ
    if (e->mem_status != IN_MEMORY)
	return 0;
#endif
    return 1;
}

static void
icpHandleIcpV2(int fd, struct sockaddr_in from, char *buf, int len)
{
    icp_common_t header;
    icp_common_t *headerp = (icp_common_t *) (void *) buf;
    StoreEntry *entry = NULL;
    char *url = NULL;
    const char *key = NULL;
    request_t *icp_request = NULL;
    int allow = 0;
    char *data = NULL;
    u_short data_sz = 0;
    u_short u;
    int pkt_len;
    aclCheck_t checklist;
    icp_common_t *reply;
    int src_rtt = 0;
    u_num32 flags = 0;

    header.opcode = headerp->opcode;
    header.version = headerp->version;
    header.length = ntohs(headerp->length);
    header.reqnum = ntohl(headerp->reqnum);
    header.flags = ntohl(headerp->flags);
    header.shostid = ntohl(headerp->shostid);
    header.pad = ntohl(headerp->pad);

    switch (header.opcode) {
    case ICP_OP_QUERY:
	nudpconn++;
	/* We have a valid packet */
	url = buf + sizeof(header) + sizeof(u_num32);
	if ((icp_request = urlParse(METHOD_GET, url)) == NULL) {
	    reply = icpCreateMessage(ICP_OP_ERR, 0, url, header.reqnum, 0);
	    icpUdpSend(fd, &from, reply, LOG_UDP_INVALID, PROTO_NONE);
	    break;
	}
	checklist.src_addr = from.sin_addr;
	checklist.request = icp_request;
	allow = aclCheckFast(Config.accessList.ICP, &checklist);
	if (!allow) {
	    debug(12, 2, "icpHandleIcpV2: Access Denied for %s by %s.\n",
		inet_ntoa(from.sin_addr), AclMatchedName);
	    if (clientdbDeniedPercent(from.sin_addr) < 95) {
		reply = icpCreateMessage(ICP_OP_DENIED, 0, url, header.reqnum, 0);
		icpUdpSend(fd, &from, reply, LOG_UDP_DENIED, icp_request->protocol);
	    }
	    break;
	}
	if (header.flags & ICP_FLAG_SRC_RTT) {
	    int rtt = netdbHostRtt(icp_request->host);
	    int hops = netdbHostHops(icp_request->host);
	    src_rtt = ((hops & 0xFFFF) << 16) | (rtt & 0xFFFF);
	    if (rtt)
		flags |= ICP_FLAG_SRC_RTT;
	}
	/* The peer is allowed to use this cache */
	entry = storeGet(storeGeneratePublicKey(url, METHOD_GET));
	debug(12, 5, "icpHandleIcpV2: OPCODE %s\n", IcpOpcodeStr[header.opcode]);
	if (icpCheckUdpHit(entry, icp_request)) {
	    pkt_len = sizeof(icp_common_t) + strlen(url) + 1 + 2 + entry->object_len;
	    if (icpCheckUdpHitObj(entry, icp_request, &header, pkt_len)) {
		reply = icpCreateHitObjMessage(ICP_OP_HIT_OBJ,
		    flags,
		    url,
		    header.reqnum,
		    src_rtt,
		    entry);
		icpUdpSend(fd, &from, reply, LOG_UDP_HIT, icp_request->protocol);
		break;
	    } else {
		reply = icpCreateMessage(ICP_OP_HIT, flags, url, header.reqnum, src_rtt);
		icpUdpSend(fd, &from, reply, LOG_UDP_HIT, icp_request->protocol);
		break;
	    }
	}
	/* if store is rebuilding, return a UDP_HIT, but not a MISS */
	if (store_rebuilding == STORE_REBUILDING_CLEAN && opt_reload_hit_only) {
	    reply = icpCreateMessage(ICP_OP_MISS_NOFETCH, flags, url, header.reqnum, src_rtt);
	    icpUdpSend(fd, &from, reply, LOG_UDP_MISS_NOFETCH, icp_request->protocol);
	} else if (hit_only_mode_until > squid_curtime) {
	    reply = icpCreateMessage(ICP_OP_MISS_NOFETCH, flags, url, header.reqnum, src_rtt);
	    icpUdpSend(fd, &from, reply, LOG_UDP_MISS_NOFETCH, icp_request->protocol);
	} else {
	    reply = icpCreateMessage(ICP_OP_MISS, flags, url, header.reqnum, src_rtt);
	    icpUdpSend(fd, &from, reply, LOG_UDP_MISS, icp_request->protocol);
	}
	break;

    case ICP_OP_HIT_OBJ:
    case ICP_OP_HIT:
    case ICP_OP_SECHO:
    case ICP_OP_DECHO:
    case ICP_OP_MISS:
    case ICP_OP_DENIED:
    case ICP_OP_MISS_NOFETCH:
	if (neighbors_do_private_keys && header.reqnum == 0) {
	    debug(12, 0, "icpHandleIcpV2: Neighbor %s returned reqnum = 0\n",
		inet_ntoa(from.sin_addr));
	    debug(12, 0, "icpHandleIcpV2: Disabling use of private keys\n");
	    neighbors_do_private_keys = 0;
	}
	url = buf + sizeof(header);
	if (header.opcode == ICP_OP_HIT_OBJ) {
	    data = url + strlen(url) + 1;
	    xmemcpy((char *) &u, data, sizeof(u_short));
	    data += sizeof(u_short);
	    data_sz = ntohs(u);
	    if ((int) data_sz > (len - (data - buf))) {
		debug(12, 0, "icpHandleIcpV2: ICP_OP_HIT_OBJ object too small\n");
		break;
	    }
	}
	debug(12, 3, "icpHandleIcpV2: %s from %s for '%s'\n",
	    IcpOpcodeStr[header.opcode],
	    inet_ntoa(from.sin_addr),
	    url);
	if (neighbors_do_private_keys && header.reqnum) {
	    key = storeGeneratePrivateKey(url, METHOD_GET, header.reqnum);
	} else {
	    key = storeGeneratePublicKey(url, METHOD_GET);
	}
	debug(12, 3, "icpHandleIcpV2: Looking for key '%s'\n", key);
	if ((entry = storeGet(key)) == NULL) {
	    debug(12, 3, "icpHandleIcpV2: Ignoring %s for NULL Entry.\n",
		IcpOpcodeStr[header.opcode]);
	} else {
	    /* call neighborsUdpAck even if ping_status != PING_WAITING */
	    neighborsUdpAck(fd,
		url,
		&header,
		&from,
		entry,
		data,
		(int) data_sz);
	}
	break;

    case ICP_OP_INVALID:
    case ICP_OP_ERR:
	break;

    default:
	debug(12, 0, "icpHandleIcpV2: UNKNOWN OPCODE: %d from %s\n",
	    header.opcode, inet_ntoa(from.sin_addr));
	break;
    }
    if (icp_request)
	put_free_request_t(icp_request);
}

/* Currently Harvest cached-2.x uses ICP_VERSION_3 */
static void
icpHandleIcpV3(int fd, struct sockaddr_in from, char *buf, int len)
{
    icp_common_t header;
    icp_common_t *reply;
    icp_common_t *headerp = (icp_common_t *) (void *) buf;
    StoreEntry *entry = NULL;
    char *url = NULL;
    const char *key = NULL;
    request_t *icp_request = NULL;
    int allow = 0;
    char *data = NULL;
    u_short data_sz = 0;
    u_short u;
    aclCheck_t checklist;

    header.opcode = headerp->opcode;
    header.version = headerp->version;
    header.length = ntohs(headerp->length);
    header.reqnum = ntohl(headerp->reqnum);
    header.flags = ntohl(headerp->flags);
    header.shostid = ntohl(headerp->shostid);

    switch (header.opcode) {
    case ICP_OP_QUERY:
	nudpconn++;
	/* We have a valid packet */
	url = buf + sizeof(header) + sizeof(u_num32);
	if ((icp_request = urlParse(METHOD_GET, url)) == NULL) {
	    reply = icpCreateMessage(ICP_OP_ERR, 0, url, header.reqnum, 0);
	    icpUdpSend(fd, &from, reply, LOG_UDP_INVALID, PROTO_NONE);
	    break;
	}
	checklist.src_addr = from.sin_addr;
	checklist.request = icp_request;
	allow = aclCheckFast(Config.accessList.ICP, &checklist);
	if (!allow) {
	    debug(12, 2, "icpHandleIcpV3: Access Denied for %s by %s.\n",
		inet_ntoa(from.sin_addr), AclMatchedName);
	    if (clientdbDeniedPercent(from.sin_addr) < 95) {
		reply = icpCreateMessage(ICP_OP_DENIED, 0, url, header.reqnum, 0);
		icpUdpSend(fd, &from, reply, LOG_UDP_DENIED, icp_request->protocol);
	    }
	    break;
	}
	/* The peer is allowed to use this cache */
	entry = storeGet(storeGeneratePublicKey(url, METHOD_GET));
	debug(12, 5, "icpHandleIcpV3: OPCODE %s\n",
	    IcpOpcodeStr[header.opcode]);
	if (icpCheckUdpHit(entry, icp_request)) {
	    reply = icpCreateMessage(ICP_OP_HIT, 0, url, header.reqnum, 0);
	    icpUdpSend(fd, &from, reply, LOG_UDP_HIT, icp_request->protocol);
	    break;
	}
	/* if store is rebuilding, return a UDP_HIT, but not a MISS */
	if (opt_reload_hit_only && store_rebuilding == STORE_REBUILDING_CLEAN) {
	    reply = icpCreateMessage(ICP_OP_MISS_NOFETCH, 0, url, header.reqnum, 0);
	    icpUdpSend(fd, &from, reply, LOG_UDP_MISS_NOFETCH, icp_request->protocol);
	} else if (hit_only_mode_until > squid_curtime) {
	    reply = icpCreateMessage(ICP_OP_MISS_NOFETCH, 0, url, header.reqnum, 0);
	    icpUdpSend(fd, &from, reply, LOG_UDP_MISS_NOFETCH, icp_request->protocol);
	} else {
	    reply = icpCreateMessage(ICP_OP_MISS, 0, url, header.reqnum, 0);
	    icpUdpSend(fd, &from, reply, LOG_UDP_MISS, icp_request->protocol);
	}
	break;

    case ICP_OP_HIT_OBJ:
    case ICP_OP_HIT:
    case ICP_OP_SECHO:
    case ICP_OP_DECHO:
    case ICP_OP_MISS:
    case ICP_OP_DENIED:
    case ICP_OP_MISS_NOFETCH:
	if (neighbors_do_private_keys && header.reqnum == 0) {
	    debug(12, 0, "icpHandleIcpV3: Neighbor %s returned reqnum = 0\n",
		inet_ntoa(from.sin_addr));
	    debug(12, 0, "icpHandleIcpV3: Disabling use of private keys\n");
	    neighbors_do_private_keys = 0;
	}
	url = buf + sizeof(header);
	if (header.opcode == ICP_OP_HIT_OBJ) {
	    data = url + strlen(url) + 1;
	    xmemcpy((char *) &u, data, sizeof(u_short));
	    data += sizeof(u_short);
	    data_sz = ntohs(u);
	    if ((int) data_sz > (len - (data - buf))) {
		debug(12, 0, "icpHandleIcpV3: ICP_OP_HIT_OBJ object too small\n");
		break;
	    }
	}
	debug(12, 3, "icpHandleIcpV3: %s from %s for '%s'\n",
	    IcpOpcodeStr[header.opcode],
	    inet_ntoa(from.sin_addr),
	    url);
	if (neighbors_do_private_keys && header.reqnum) {
	    key = storeGeneratePrivateKey(url, METHOD_GET, header.reqnum);
	} else {
	    key = storeGeneratePublicKey(url, METHOD_GET);
	}
	debug(12, 3, "icpHandleIcpV3: Looking for key '%s'\n", key);
	if ((entry = storeGet(key)) == NULL) {
	    debug(12, 3, "icpHandleIcpV3: Ignoring %s for NULL Entry.\n",
		IcpOpcodeStr[header.opcode]);
	} else {
	    /* call neighborsUdpAck even if ping_status != PING_WAITING */
	    neighborsUdpAck(fd,
		url,
		&header,
		&from,
		entry,
		data,
		(int) data_sz);
	}
	break;

    case ICP_OP_INVALID:
    case ICP_OP_ERR:
	break;

    default:
	debug(12, 0, "icpHandleIcpV3: UNKNOWN OPCODE: %d from %s\n",
	    header.opcode, inet_ntoa(from.sin_addr));
	break;
    }
    if (icp_request)
	put_free_request_t(icp_request);
}

#ifdef ICP_PKT_DUMP
static void
icpPktDump(icp_common_t * pkt)
{
    struct in_addr a;

    debug(12, 9, "opcode:     %3d %s\n",
	(int) pkt->opcode,
	IcpOpcodeStr[pkt->opcode]);
    debug(12, 9, "version: %-8d\n", (int) pkt->version);
    debug(12, 9, "length:  %-8d\n", (int) ntohs(pkt->length));
    debug(12, 9, "reqnum:  %-8d\n", ntohl(pkt->reqnum));
    debug(12, 9, "flags:   %-8x\n", ntohl(pkt->flags));
    a.s_addr = ntohl(pkt->shostid);
    debug(12, 9, "shostid: %s\n", inet_ntoa(a));
    debug(12, 9, "payload: %s\n", (char *) pkt + sizeof(icp_common_t));
}
#endif

void
icpHandleUdp(int sock, void *not_used)
{
    struct sockaddr_in from;
    int from_len;
    LOCAL_ARRAY(char, buf, SQUID_UDP_SO_RCVBUF);
    int len;
    icp_common_t *headerp = NULL;
    int icp_version;

    commSetSelect(sock, COMM_SELECT_READ, icpHandleUdp, NULL, 0);
    from_len = sizeof(from);
    memset(&from, 0, from_len);
    len = recvfrom(sock,
	buf,
	SQUID_UDP_SO_RCVBUF - 1,
	0,
	(struct sockaddr *) &from,
	&from_len);
    if (len < 0) {
#ifdef _SQUID_LINUX_
	/* Some Linux systems seem to set the FD for reading and then
	 * return ECONNREFUSED when sendto() fails and generates an ICMP
	 * port unreachable message. */
	/* or maybe an EHOSTUNREACH "No route to host" message */
	if (errno != ECONNREFUSED && errno != EHOSTUNREACH)
#endif
	    debug(50, 1, "icpHandleUdp: FD %d recvfrom: %s\n",
		sock, xstrerror());
	return;
    }
    buf[len] = '\0';
    debug(12, 4, "icpHandleUdp: FD %d: received %d bytes from %s.\n",
	sock,
	len,
	inet_ntoa(from.sin_addr));
#ifdef ICP_PACKET_DUMP
    icpPktDump(buf);
#endif
    if (len < sizeof(icp_common_t)) {
	debug(12, 4, "icpHandleUdp: Ignoring too-small UDP packet\n");
	return;
    }
    headerp = (icp_common_t *) (void *) buf;
    if ((icp_version = (int) headerp->version) == ICP_VERSION_2)
	icpHandleIcpV2(sock, from, buf, len);
    else if (icp_version == ICP_VERSION_3)
	icpHandleIcpV3(sock, from, buf, len);
    else
	debug(12, 0, "WARNING: Unused ICP version %d received from %s:%d\n",
	    icp_version,
	    inet_ntoa(from.sin_addr),
	    ntohs(from.sin_port));
}

/*
 *  parseHttpRequest()
 * 
 *  Called by
 *    clientReadRequest() after the request has been read
 *  Calls
 *    do_append_domain()
 *  Returns
 *   -1 on error
 *    0 on incomplete request
 *    1 on success
 */
static int
parseHttpRequest(clientHttpRequest * http, method_t * method_p)
{
    char *inbuf = NULL;
    char *mstr = NULL;
    char *url = NULL;
    char *req_hdr = NULL;
    LOCAL_ARRAY(char, http_ver, 32);
    char *token = NULL;
    char *t = NULL;
    char *s = NULL;
    int free_request = 0;
    int req_hdr_sz;
    int url_sz;
    method_t method;

    /* Make sure a complete line has been received */
    if (strchr(http->in.buf, '\n') == NULL) {
	debug(12, 5, "Incomplete request line, waiting for more data\n");
	return 0;
    }
    /* Use xmalloc/xmemcpy instead of xstrdup because inbuf might
     * contain NULL bytes; especially for POST data  */
    inbuf = xmalloc(http->in.offset + 1);
    xmemcpy(inbuf, http->in.buf, http->in.offset);
    *(inbuf + http->in.offset) = '\0';

    /* Look for request method */
    if ((mstr = strtok(inbuf, "\t ")) == NULL) {
	debug(12, 1, "parseHttpRequest: Can't get request method\n");
	xfree(inbuf);
	return -1;
    }
    method = urlParseMethod(mstr);
    if (method == METHOD_NONE) {
	debug(12, 1, "parseHttpRequest: Unsupported method '%s'\n", mstr);
	xfree(inbuf);
	return -1;
    }
    debug(12, 5, "parseHttpRequest: Method is '%s'\n", mstr);

    /* look for URL */
    if ((url = strtok(NULL, "\r\n\t ")) == NULL) {
	debug(12, 1, "parseHttpRequest: Missing URL\n");
	xfree(inbuf);
	return -1;
    }
    debug(12, 5, "parseHttpRequest: Request is '%s'\n", url);

    token = strtok(NULL, null_string);
    for (t = token; t && *t && *t != '\n' && *t != '\r'; t++);
    if (t == NULL || *t == '\0' || t == token) {
	debug(12, 3, "parseHttpRequest: Missing HTTP identifier\n");
	xfree(inbuf);
	return -1;
    }
    memset(http_ver, '\0', 32);
    xstrncpy(http_ver, token, 32);
    sscanf(http_ver, "HTTP/%f", &http->http_ver);
    debug(12, 5, "parseHttpRequest: HTTP version is '%s'\n", http_ver);

    /* Check if headers are received */
    if (!mime_headers_end(t)) {
	xfree(inbuf);
	return 0;		/* not a complete request */
    }
    while (isspace(*t))
	t++;
    req_hdr = t;
    req_hdr_sz = http->in.offset - (req_hdr - inbuf);

    /* Ok, all headers are received */
    http->req_hdr_sz = req_hdr_sz;
    http->request_hdr = xmalloc(req_hdr_sz + 1);
    xmemcpy(http->request_hdr, req_hdr, req_hdr_sz);
    *(http->request_hdr + req_hdr_sz) = '\0';

    debug(12, 5, "parseHttpRequest: Request Header is\n%s\n",
	http->request_hdr);

    /* Assign http->url */
    if ((t = strchr(url, '\n')))	/* remove NL */
	*t = '\0';
    if ((t = strchr(url, '\r')))	/* remove CR */
	*t = '\0';
    if ((t = strchr(url, '#')))	/* remove HTML anchors */
	*t = '\0';

    /* see if we running in httpd_accel_mode, if so got to convert it to URL */
    if (httpd_accel_mode && *url == '/') {
	/* prepend the accel prefix */
	if (vhost_mode) {
	    /* Put the local socket IP address as the hostname */
	    url_sz = strlen(url) + 32 + Config.appendDomainLen;
	    http->url = xcalloc(url_sz, 1);
	    sprintf(http->url, "http://%s:%d%s",
		inet_ntoa(http->conn->me.sin_addr),
		(int) Config.Accel.port,
		url);
	    debug(12, 5, "VHOST REWRITE: '%s'\n", http->url);
	} else if (opt_accel_uses_host && (t = mime_get_header(req_hdr, "Host"))) {
	    /* If a Host: header was specified, use it to build the URL 
	     * instead of the one in the Config file. */
	    /*
	     * XXX Use of the Host: header here opens a potential
	     * security hole.  There are no checks that the Host: value
	     * corresponds to one of your servers.  It might, for example,
	     * refer to www.playboy.com.  The 'dst' and/or 'dst_domain' ACL 
	     * types should be used to prevent httpd-accelerators 
	     * handling requests for non-local servers */
	    if ((s = strchr(t, ':')))
		*s = '\0';
	    url_sz = strlen(url) + 32 + Config.appendDomainLen;
	    http->url = xcalloc(url_sz, 1);
	    sprintf(http->url, "http://%s:%d%s",
		t, (int) Config.Accel.port, url);
	} else {
	    url_sz = strlen(Config.Accel.prefix) + strlen(url) +
		Config.appendDomainLen + 1;
	    http->url = xcalloc(url_sz, 1);
	    sprintf(http->url, "%s%s", Config.Accel.prefix, url);
	}
	http->accel = 1;
    } else {
	/* URL may be rewritten later, so make extra room */
	url_sz = strlen(url) + Config.appendDomainLen + 5;
	http->url = xcalloc(url_sz, 1);
	strcpy(http->url, url);
	http->accel = 0;
    }

    debug(12, 5, "parseHttpRequest: Complete request received\n");
    if (free_request)
	safe_free(url);
    xfree(inbuf);
    *method_p = method;
    return 1;
}

#define ASCII_INBUF_BLOCKSIZE 4096

static void
clientReadRequest(int fd, void *data)
{
    clientHttpRequest *http = data;
    int parser_return_code = 0;
    int k;
    request_t *request = NULL;
    char *wbuf = NULL;
    int size;
    int len;
    method_t method;

    len = http->in.size - http->in.offset - 1;
    debug(12, 4, "clientReadRequest: FD %d: reading request...\n", fd);
    debug(12, 4, "clientReadRequest: len = %d\n", len);
    size = read(fd, http->in.buf + http->in.offset, len);
debug(0,0,"clientReadRequest: read %d bytes\n", size);
    fd_bytes(fd, size, FD_READ);

    if (size == 0) {
        http->conn->nrequests--;
	comm_close(fd);
	return;
    } else if (size < 0) {
	if (errno == EWOULDBLOCK || errno == EAGAIN) {
	    debug(50, 0, "clientReadRequest: FD %d: %s\n", fd, xstrerror());
	    commSetSelect(fd,
		COMM_SELECT_READ,
		clientReadRequest,
		http,
		0);
	} else {
	    debug(50, 2, "clientReadRequest: FD %d: %s\n", fd, xstrerror());
            http->conn->nrequests--;
	    comm_close(fd);
	}
	return;
    }
    http->in.offset += size;
    http->in.buf[http->in.offset] = '\0';	/* Terminate the string */

    parser_return_code = parseHttpRequest(http, &method);
    if (parser_return_code == 1) {
	commSetTimeout(fd, Config.Timeout.lifetime, NULL, NULL);
	if ((request = urlParse(method, http->url)) == NULL) {
	    debug(12, 5, "Invalid URL: %s\n", http->url);
	    wbuf = squid_error_url(http->url,
		method,
		ERR_INVALID_URL,
		fd_table[fd].ipaddr,
		400,
		NULL);
	    icpSendERROR(fd, ERR_INVALID_URL, wbuf, http, 400);
	    return;
	}
	request->http_ver = http->http_ver;
	if (!urlCheckRequest(request)) {
	    http->log_type = ERR_UNSUP_REQ;
	    http->http_code = 501;
	    wbuf = xstrdup(squid_error_url(http->url,
		    method,
		    ERR_UNSUP_REQ,
		    fd_table[fd].ipaddr,
		    http->http_code,
		    NULL));
	    comm_write(fd,
		wbuf,
		strlen(wbuf),
		icpSendERRORComplete,
		http,
		xfree);
	    return;
	}
	http->request = requestLink(request);
	clientAccessCheck(http);
	if (!BIT_TEST(request->flags, REQ_PROXY_KEEPALIVE)) {
	    commSetSelect(fd,
		COMM_SELECT_READ,
		icpDetectClientClose,
		http->conn,
		0);
	}
    } else if (parser_return_code == 0) {
	/*
	 *    Partial request received; reschedule until parseAsciiUrl()
	 *    is happy with the input
	 */
	k = http->in.size - 1 - http->in.offset;
	if (k == 0) {
	    if (http->in.offset >= Config.maxRequestSize) {
		/* The request is too large to handle */
		debug(12, 0, "clientReadRequest: Request won't fit in buffer.\n");
		debug(12, 0, "-->     max size = %d\n", Config.maxRequestSize);
		debug(12, 0, "--> http->in.offset = %d\n", http->in.offset);
		icpSendERROR(fd,
		    ERR_INVALID_REQ,
		    "error reading request",
		    http,
		    400);
		return;
	    }
	    /* Grow the request memory area to accomodate for a large request */
	    http->in.size += ASCII_INBUF_BLOCKSIZE;
	    http->in.buf = xrealloc(http->in.buf, http->in.size);
	    meta_data.misc += ASCII_INBUF_BLOCKSIZE;
	    debug(12, 2, "Handling a large request, offset=%d inbufsize=%d\n",
		http->in.offset, http->in.size);
	    k = http->in.size - 1 - http->in.offset;
	}
	commSetSelect(fd,
	    COMM_SELECT_READ,
	    clientReadRequest,
	    http,
	    0);
    } else {
	/* parser returned -1 */
	debug(12, 1, "clientReadRequest: FD %d Invalid Request\n", fd);
	wbuf = squid_error_request(http->in.buf,
	    ERR_INVALID_REQ,
	    400);
	icpSendERROR(fd, ERR_INVALID_REQ, wbuf, http, 400);
    }
}


/* general lifetime handler for HTTP requests */
static void
requestTimeout(int fd, void *data)
{
    clientHttpRequest *http = data;
    StoreEntry *entry = http->entry;
    debug(12, 2, "requestTimeout: FD %d: lifetime is expired.\n", fd);
    CheckQuickAbort(http);
    if (entry)
	storeUnregister(entry, http);
    /* There might be a comm_write() thread; cancel callback */
    if (fd_table[fd].rwstate) {
	/* Some data has been sent to the client, just cancel the
	 * callback and close the FD */
	commCancelWriteHandler(fd);
	comm_close(fd);
    } else {
	/* Send an error message if nothing has been sent yet */
	icpSendERROR(fd,
	    ERR_LIFETIME_EXP,
	    "Client Lifetime Expired",
	    http,
	    504);
    }
}

/* Handle a new connection on ascii input socket. */
void
asciiHandleConn(int sock, void *notused)
{
    int fd = -1;
    ConnStateData *connState = NULL;
    struct sockaddr_in peer;
    struct sockaddr_in me;
    memset(&peer, '\0', sizeof(struct sockaddr_in));
    memset(&me, '\0', sizeof(struct sockaddr_in));
    commSetSelect(sock, COMM_SELECT_READ, asciiHandleConn, NULL, 0);
    if ((fd = comm_accept(sock, &peer, &me)) < 0) {
	debug(50, 1, "asciiHandleConn: FD %d: accept failure: %s\n",
	    sock, xstrerror());
	return;
    }
    if (vizSock > -1)
	vizHackSendPkt(&peer, 1);
    debug(12, 4, "asciiHandleConn: FD %d: accepted\n", fd);
    connState = xcalloc(1, sizeof(ConnStateData));
    connState->peer = peer;
    connState->log_addr = peer.sin_addr;
    connState->log_addr.s_addr &= Config.Addrs.client_netmask.s_addr;
    connState->me = me;
    connState->fd = fd;
    connState->ident.fd = -1;
    comm_add_close_handler(fd, connStateFree, connState);
    if (Config.Log.log_fqdn)
	fqdncache_gethostbyaddr(peer.sin_addr, FQDN_LOOKUP_IF_MISS);
    icpDetectNewRequest(fd, connState);
}

void
AppendUdp(icpUdpData * item)
{
    item->next = NULL;
    if (UdpQueueHead == NULL) {
	UdpQueueHead = item;
	UdpQueueTail = item;
    } else if (UdpQueueTail == UdpQueueHead) {
	UdpQueueTail = item;
	UdpQueueHead->next = item;
    } else {
	UdpQueueTail->next = item;
	UdpQueueTail = item;
    }
}

/* return 1 if the request should be aborted */
static int
CheckQuickAbort2(const clientHttpRequest * http)
{
    long curlen;
    long minlen;
    long expectlen;

    if (!BIT_TEST(http->request->flags, REQ_CACHABLE))
	return 1;
    if (BIT_TEST(http->entry->flag, KEY_PRIVATE))
	return 1;
    if (http->entry->mem_obj == NULL)
	return 1;
    expectlen = http->entry->mem_obj->reply->content_length;
    curlen = http->entry->mem_obj->e_current_len;
    minlen = Config.quickAbort.min;
    if (minlen < 0)
	/* disabled */
	return 0;
    if (curlen > expectlen)
	/* bad content length */
	return 1;
    if ((expectlen - curlen) < minlen)
	/* only little more left */
	return 0;
    if ((expectlen - curlen) > Config.quickAbort.max)
	/* too much left to go */
	return 1;
    if ((curlen / (expectlen / 128U)) > Config.quickAbort.pct)
	/* past point of no return */
	return 0;
    return 1;
}


static void
CheckQuickAbort(clientHttpRequest * http)
{
    if (http->entry == NULL)
	return;
    if (storePendingNClients(http->entry) > 1)
	return;
    if (http->entry->store_status == STORE_OK)
	return;
    if (CheckQuickAbort2(http) == 0)
	return;
    BIT_SET(http->entry->flag, CLIENT_ABORT_REQUEST);
    storeReleaseRequest(http->entry);
    http->log_type = ERR_CLIENT_ABORT;
}

static int
icpCheckTransferDone(clientHttpRequest * http)
{
    StoreEntry *entry = http->entry;
    MemObject *mem = NULL;

    if (entry == NULL)
	return 0;
    if (entry->store_status != STORE_PENDING)
	if (http->out.offset >= entry->object_len)
	    return 1;
    if ((mem = entry->mem_obj) == NULL)
	return 0;
    if (mem->reply->content_length == 0)
	return 0;
    if (http->out.offset >= mem->reply->content_length + mem->reply->hdr_sz)
	return 1;
    return 0;
}

void
icpDetectClientClose(int fd, void *data)
{
    ConnStateData *connState = data;
    clientHttpRequest *http = connState->chr;
    LOCAL_ARRAY(char, buf, 256);
    int n;
    StoreEntry *entry = http->entry;
    errno = 0;
    if (icpCheckTransferDone(http)) {
	/* All data has been delivered */
	debug(12, 5, "icpDetectClientClose: FD %d end of transmission\n", fd);
	HTTPCacheInfo->proto_touchobject(HTTPCacheInfo,
	    HTTPCacheInfo->proto_id(entry->url),
	    http->out.size);
	comm_close(fd);
    } else if ((n = read(fd, buf, 255)) > 0) {
	fd_bytes(fd, n, FD_READ);
	buf[n] = '\0';
	debug(12, 0, "icpDetectClientClose: FD %d, %d unexpected bytes\n",
	    fd, n);
	debug(12, 1, "--> from: %s\n", fd_table[fd].ipaddr);
	debug(12, 1, "--> data: %s\n", rfc1738_escape(buf));
	commSetSelect(fd,
	    COMM_SELECT_READ,
	    icpDetectClientClose,
	    connState,
	    0);
    } else if (n < 0 && (errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR)) {
	commSetSelect(fd,
	    COMM_SELECT_READ,
	    icpDetectClientClose,
	    connState,
	    0);
    } else if (n < 0) {
	debug(12, 5, "icpDetectClientClose: FD %d\n", fd);
	debug(12, 5, "--> URL '%s'\n", http->url);
	if (errno == ECONNRESET)
	    debug(50, 2, "icpDetectClientClose: ERROR %s: %s\n",
		fd_table[fd].ipaddr, xstrerror());
	else if (errno)
	    debug(50, 1, "icpDetectClientClose: ERROR %s: %s\n",
		fd_table[fd].ipaddr, xstrerror());
	CheckQuickAbort(http);
	if (entry) {
	    if (entry->ping_status == PING_WAITING)
		storeReleaseRequest(entry);
	    storeUnregister(entry, http);
	    storeRegister(entry, fd, icpHandleAbort, connState);
	}
	protoUnregister(fd,
	    entry,
	    http->request,
	    http->conn->peer.sin_addr);
	comm_close(fd);
    } else {
	debug(12, 5, "icpDetectClientClose: FD %d closed?\n", fd);
	comm_set_stall(fd, 10);	/* check again in 10 seconds */
	commSetSelect(fd, COMM_SELECT_READ, icpDetectClientClose, connState, 0);
    }
}

static void
icpDetectNewRequest(int fd, void *data)
{
    ConnStateData *connState = data;
    clientHttpRequest *http;
    clientHttpRequest **H;
    ntcpconn++;
    connState->nrequests++;
    if (vizSock > -1)
	vizHackSendPkt(&connState->peer, 1);
    if (fd != connState->fd)
	fatal_dump("icpDetectNewRequest: FD mismatch");
    debug(0, 0, "icpDetectNewRequest: FD %d: accepted\n", fd);
    http = xcalloc(1, sizeof(clientHttpRequest));
    http->start = current_time;
    http->in.size = ASCII_INBUF_BLOCKSIZE;
    http->in.buf = xcalloc(http->in.size, 1);
    http->entry = NULL;
    http->conn = connState;
    meta_data.misc += ASCII_INBUF_BLOCKSIZE;
    fd_note(fd, "Reading Request");
    commSetTimeout(fd, Config.Timeout.read, requestTimeout, http);
    commSetSelect(fd,
        COMM_SELECT_READ,
        clientReadRequest,
        http,
        0);
    commSetSelect(fd,
	COMM_SELECT_READ,
	clientReadRequest,
	http,
	0);
    for (H = &connState->chr; *H; H = &(*H)->next);
    *H = http;
}

static char *
icpConstruct304reply(struct _http_reply *source)
{
    LOCAL_ARRAY(char, line, 256);
    LOCAL_ARRAY(char, reply, 8192);

    memset(reply, '\0', 8192);
    strcpy(reply, "HTTP/1.0 304 Not Modified\r\n");
    if (source->date > -1) {
	sprintf(line, "Date: %s\r\n", mkrfc1123(source->date));
	strcat(reply, line);
    }
    if ((int) strlen(source->content_type) > 0) {
	sprintf(line, "Content-type: %s\r\n", source->content_type);
	strcat(reply, line);
    }
    if (source->content_length) {
	sprintf(line, "Content-length: %d\r\n", source->content_length);
	strcat(reply, line);
    }
    if (source->expires > -1) {
	sprintf(line, "Expires: %s\r\n", mkrfc1123(source->expires));
	strcat(reply, line);
    }
    if (source->last_modified > -1) {
	sprintf(line, "Last-modified: %s\r\n",
	    mkrfc1123(source->last_modified));
	strcat(reply, line);
    }
    strcat(reply, "\r\n");
    return reply;
}

struct viz_pkt {
    u_num32 from;
    char type;
};

void
vizHackSendPkt(const struct sockaddr_in *from, int type)
{
    static struct viz_pkt v;

    v.from = from->sin_addr.s_addr;
    v.type = (char) type;
    sendto(vizSock,
	(char *) &v,
	sizeof(v),
	0,
	(struct sockaddr *) &Config.vizHack.S,
	sizeof(struct sockaddr_in));
}

/* 
 * icpHandleAbort()
 * Call for objects which might have been aborted.  If the entry
 * was aborted, AND the client has not seen any data yet, then
 * Queue the error page via icpSendERROR().  Otherwise just
 * close the socket.
 */
static void
icpHandleAbort(void *data)
{
    clientHttpRequest *http = data;
    StoreEntry *entry = http->entry;
    int fd = http->conn->fd;
    if (entry == NULL) {
	comm_close(fd);
	return;
    }
    if (entry->store_status != STORE_ABORTED) {
	comm_close(fd);
	return;
    }
    if (http->out.size > 0) {
	comm_close(fd);
	return;
    }
    if (entry->mem_obj->e_abort_msg == NULL) {
	comm_close(fd);
	return;
    }
    icpSendERROR(fd,
	entry->mem_obj->abort_code,
	entry->mem_obj->e_abort_msg,
	http,
	400);
}
