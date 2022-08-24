/*
 * This is part of the GNU GMAN Library, a FREE implementation of the
 * RenderMan Interface Specification.
 *
 * Copyright (c) 2001, 2000, 1999, John Cairns 
 *
 * Author: John Cairns <john@2ad.com>
 */

/*
  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Library General Public
  License as published by the Free Software Foundation; either
  version 2 of the License, or (at your option) any later version.
  

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Library General Public License for more details.
  
  You should have received a copy of the GNU Library General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

  To contact the author of GNU GMAN, write to John Cairns, 607 E STUART ST,
  FT COLLINS, CO, 80525, USA, or write via E-mail john@2ad.com.
*/

/* system headers */
#include <stdlib.h>
#include <stdio.h>
#include <iostream.h>
#include <cstring>

/* gman headers */
#include "ri.h"
#include "gmanerror.h"
#include "gmansl.h"

/* global macros */
#ifndef PATH_MAX
#define PATH_MAX 255
#endif


/* global variables */
int      lineNumber;
char    *slSourceFile;
char    slBaseName [PATH_MAX];
FILE	*outHeaderFile=NULL,  *outSourceFile=NULL, *logFile=NULL;

/* global function prototypes */

int yyparse(); // on behalf of yacc

int main(int argc, char *argv[]) {

  int rc = EXIT_SUCCESS;
  // handle command line arguments
  if(argc < 2) {
    usage(argv[0]);
  } else {
    
    for(int i=1; i<argc; i++) {
      
      cout << "Compiling... " << argv[i];
      
      if(compile(argv[i])) {
	cout << "ok.";
      } else {
	cout << "failed.";
	rc = EXIT_FAILURE;
      }
      cout << endl;
    }
  }
  
  return rc;
}

RtVoid usage(char *myname) {
  
  cerr << myname << ": files ..." << endl;
  cerr << "\tCompile shading language modules." << endl;

}


RtBoolean compile(char *shader) {
  char headerName[PATH_MAX],
    srcName[PATH_MAX],
    slSourceName[PATH_MAX],
    logFileName[PATH_MAX];

  extern FILE *yyin, *yyout;
  
  // for now we assume the shader name passed is the name to
  // the source, it would be nicer if this involved some type
  // of path search...
  
  yyin = fopen(shader, "r"); // open shader to parse
  if(yyin == NULL) {
    // if we can't open it, give up
    return RI_FALSE;
  }

  slSourceFile = shader;
  
  // find the root name of the shader, assuming
  // shader ends in .sl
  
  strcpy(shader, slSourceName);
  
  // find the . and terminate it
  int i;
  for(i=strlen(slSourceName);
      i>0;
      i--) {
    if(slSourceName[i] == '.') {
      slSourceName[i] = (char)0;
      break;
    }
  }
  
  // now find the first '/'
  for(;i>0; i--) {
    if(slSourceName[i] == '/')
      break;
  }

  // copy from i -> end into baseName
  if(slSourceName[i]=='/') {
    strcpy(slSourceName + i + 1, slBaseName);
  } else {
    strcpy(slSourceName, slBaseName);
  }
  
  // the header name
  sprintf(headerName, "%s.h", slSourceName);
  // the source name
  sprintf(srcName,   "%s.cpp", slSourceName);
  // log file name
  sprintf(logFileName, "%s.log", slSourceName);
  
  logFile = fopen(logFileName, "w");

  if(logFile != NULL) {
  
    lineNumber = 0;
  
    yyerror("Starting.");
  
    // open a file for writing the shading language header
    outHeaderFile = initHeader(headerName);
    
    // open a file for writing the shading language source code
    outSourceFile = initSource(headerName, srcName);
    
    if(outHeaderFile && outSourceFile) {
      yyparse();
      
      // close the source files so they can be built
      fclose(outHeaderFile);
      fclose(outSourceFile);
      
      // build the shader into a library
      build(srcName);
    } else {
      yyerror("Could not open output header or source file.");
    }
    yyerror("Done.");
    fclose(logFile);
  }
  return RI_TRUE;
}


void yyerror(char *s) {
  fprintf(logFile, "%s, %d: %s", slSourceFile, lineNumber, s);
}


FILE *initHeader(char *headerName) {
  
  /* open the header file, give it some comments and get it
   * generally ready to become a DSO shader.
   */

  yyerror("Opening shader output header.");
  
  FILE *headerFile = fopen(headerName, "w");
  if(headerFile) {
    fprintf(headerFile, 
	    "/* This is part of the GNU GMAN Library, a FREE implementation of the\n"
	    " * RenderMan Interface Specification.\n"
	    " *\n"
	    " * Copyright (c) 2001, 2000, 1999, John Cairns\n"
	    " *\n"
	    " * Author: John Cairns <john@2ad.com>\n"
	    " */\n"
	    "\n"
	    "/*\n"
	    "  This library is free software; you can redistribute it and/or\n"
	    "  modify it under the terms of the GNU Library General Public\n"
	    "  License as published by the Free Software Foundation; either\n"
	    "  version 2 of the License, or (at your option) any later version.\n"
  

	    "  This library is distributed in the hope that it will be useful,\n"
	    "  but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
	    "  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU\n"
	    "  Library General Public License for more details.\n"
	    "\n"
	    "  You should have received a copy of the GNU Library General Public\n"
	    "  License along with this library; if not, write to the Free Software\n"
	    "  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.\n"
	    "\n"
	    "  To contact the author of GNU GMAN, write to John Cairns, 607 E STUART ST,\n"
	    "  FT COLLINS, CO, 80525, USA, or write via E-mail john@2ad.com.\n"
	    " */\n"
	    );

    fprintf(headerFile, "\n\n#ifndef __GMAN_%s_H\n"
	    "#define __GMAN_%s_H 1\n\n"
	    "/*\n"
	    " * This code generated by gmansl, the GMAN shading language\n"
	    " * compiler.\n"
	    " *\n"
	    " * This class represents the %s shader.\n"
	    " */\n\n",
	    slBaseName,
	    slBaseName, 
	    slBaseName);

  }
  return headerFile;

}

FILE *initSource(char *headerName, char *srcName) {
  yyerror("Opening shader output source.");

}


RtVoid build(char *srcName) {

}
