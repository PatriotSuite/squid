/*  $Id: url.h,v 1.5 1996/04/11 22:53:42 wessels Exp $ */

#ifndef _URL_HEADER_
#define _URL_HEADER_

typedef enum {
    METHOD_NONE,
    METHOD_GET,
    METHOD_POST,
    METHOD_HEAD
} method_t;

extern char *RequestMethodStr[];

typedef enum {
    PROTO_NONE,
    PROTO_HTTP,
    PROTO_FTP,
    PROTO_GOPHER,
    PROTO_WAIS,
    PROTO_CACHEOBJ,
    PROTO_MAX
} protocol_t;

extern char *ProtocolStr[];


#define MAX_URL  (ICP_MAX_URL)

extern char *url_convert_hex _PARAMS((char *org_url, int allocate));
extern char *url_escape _PARAMS((char *url));
extern protocol_t urlParseProtocol _PARAMS((char *));
extern method_t urlParseMethod _PARAMS((char *));
extern int urlDefaultPort _PARAMS((protocol_t));

#endif /* _URL_HEADER_ */
