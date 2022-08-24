## Process this file with automake to produce Makefile.in.
##
##  	Copyright (C) 1998 Free Software Foundation, Inc.
##
##   This file is part of GMAN.
##   
##   GMAN is free software; you can redistribute it and/or modify
##   it under the terms of the GNU Lesser General Public License as
##   published by the Free Software Foundation; either version 2.1, or
##   (at your option) any later version.
##   
##   GMAN is distributed in the hope that it will be useful, but
##   WITHOUT ANY WARRANTY; without even the implied warranty of
##   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
##   GNU Lesser General Public License for more details.
##   
##   You should have received a copy of the GNU Lesser General Public
##   License along with GMAN; see the file COPYING.  If not, write
##   to the Free Software Foundation, Inc., 59 Temple Place, Suite
##   330, Boston, MA 02111-1307 USA

AUTOMAKE_OPTIONS = foreign

INCLUDES = -I../../include -I/usr/X11R6/include

include_HEADERS = \
	gmanreyesrenderer.h

lib_LTLIBRARIES =  libgmanreyes.la

libgmanreyes_la_LDFLAGS= -version-info @GMAN_SO_VERSION@ -no-undefined

libgmanreyes_la_SOURCES = \
	gmanreyesrenderer.cpp \
	reyesloader.cpp

