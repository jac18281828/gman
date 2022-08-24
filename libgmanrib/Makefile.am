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

VERSION  = 0.1.0

INCLUDES = -I. -I../include/

lib_LTLIBRARIES =  libgmanrib.la

libgmanrib_la_LDFLAGS= -version-info @GMAN_SO_VERSION@ -no-undefined

libgmanrib_la_SOURCES = \
	gmanascii.cpp \
	gmancapi.cpp \
	gmancontext.cpp \
	gmandictionary.cpp \
	gmanerror.cpp \
	gmangetarguments.cpp \
	gmaninlineparse.cpp \
	gmanrenderman.cpp \
	gmantoken.cpp

include_HEADERS = \
	gmanascii.h \
	gmancontext.h \
	gmandictionary.h \
	gmanerror.h \
	gmangetarguments.h \
	gmaninlineparse.h \
	gmanrenderman.h \
	../include/gmantypes.h \
	../include/ri.h
