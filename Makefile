# Makefile for SQUID Cache
#
# $Id: Makefile,v 1.6 1996/07/09 03:41:01 wessels Exp $
#

prefix		= /usr/local/squid
exec_prefix     = '$${prefix}'

SHELL		= /bin/sh

all: makefile
	@${MAKE} -f makefile all

squid: makefile dosquid

dosquid:
	@${MAKE} -f makefile squid

.DEFAULT:
	@if test \! -f makefile; then ${MAKE} makefile; fi
	@${MAKE} -f makefile $@

makefile: makefile.in Makefile
	@echo Running configure script to generate makefile
	./configure --prefix=${prefix} --exec_prefix=${exec_prefix}
# DO NOT DELETE
