
/*
 * $Id: errorpage.h,v 1.17 1996/09/15 05:04:22 wessels Exp $
 *
 * AUTHOR: Duane Wessels
 *
 * SQUID Internet Object Cache  http://www.nlanr.net/Squid/
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

extern void squid_error_entry __P((StoreEntry *, log_type, char *));
extern char *squid_error_url __P((char *, int, int, char *, int, char *));
extern char *squid_error_request __P((char *, int, char *, int));
extern void errorInitialize __P((void));
extern char *access_denied_msg __P((int, int, char *, char *));
extern char *access_denied_redirect __P((int, int, char *, char *, char *));
#if USE_PROXY_AUTH
extern char *proxy_denied_msg __P((int, int, char *, char *));
#endif /* USE_PROXY_AUTH */
extern char *authorization_needed_msg __P((request_t *, char *));

extern char *tmp_error_buf;
