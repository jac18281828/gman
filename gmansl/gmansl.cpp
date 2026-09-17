/* SPDX-License-Identifier: LGPL-2.1-or-later */

/*
 * This is part of GMAN, a RenderMan-compatible renderer.
 *
 * Copyright (c) 2001, 2000, 1999, John Cairns 
 *
 * Author: John Cairns <john@2ad.com>
 */

/*
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
 */

#include <filesystem>
#include <iostream>
#include <string>

#include <stdio.h>
#include <stdlib.h>

#include "gmanerror.h"
#include "gmansl.h"
#include "ri.h"

/* global variables */
int      lineNumber;
char    *slSourceFile;
std::string slBaseName;
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
      
      std::cout << "Compiling... " << argv[i];
      
      if(compile(argv[i])) {
	std::cout << "ok.";
      } else {
	std::cout << "failed.";
	rc = EXIT_FAILURE;
      }
      std::cout << std::endl;
    }
  }
  
  return rc;
}

RtVoid usage(char *myname) {
  
  std::cerr << myname << ": files ..." << std::endl;
  std::cerr << "\tCompile shading language modules." << std::endl;

}


RtBoolean compile(char *shader) {
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

  // the source name is the shader path with its extension removed; the
  // base name is that path's stem, with no directory and no extension.
  const std::filesystem::path sourcePath =
      std::filesystem::path(shader).replace_extension();
  const std::string slSourceName = sourcePath.string();
  slBaseName = sourcePath.stem().string();

  // header, source and log names are the source name plus their suffix.
  const std::string headerName = slSourceName + ".h";
  const std::string srcName = slSourceName + ".cpp";
  const std::string logFileName = slSourceName + ".log";

  logFile = fopen(logFileName.c_str(), "w");

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


FILE *initHeader(const std::string &headerName) {

  /* open the header file, give it some comments and get it
   * generally ready to become a DSO shader.
   */

  yyerror("Opening shader output header.");

  FILE *headerFile = fopen(headerName.c_str(), "w");
  if(headerFile) {
    fprintf(headerFile, 
	    "/* SPDX-License-Identifier: LGPL-2.1-or-later\n"
	    " *\n"
	    " * This is part of GMAN, a RenderMan-compatible renderer.\n"
	    " *\n"
	    " * Copyright (c) 2001, 2000, 1999, John Cairns <john@2ad.com>\n"
	    " */\n"
	    "\n"
	    "/*\n"
	    " * This library is free software; you can redistribute it and/or\n"
	    " * modify it under the terms of the GNU Lesser General Public\n"
	    " * License as published by the Free Software Foundation; either\n"
	    " * version 2.1 of the License, or (at your option) any later version.\n"
	    " *\n"
	    " * This library is distributed in the hope that it will be useful,\n"
	    " * but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
	    " * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU\n"
	    " * Lesser General Public License for more details.\n"
	    " *\n"
	    " * You should have received a copy of the GNU Lesser General Public\n"
	    " * License along with this library; if not, write to the Free Software\n"
	    " * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA\n"
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
	    slBaseName.c_str(),
	    slBaseName.c_str(),
	    slBaseName.c_str());

  }
  return headerFile;

}

FILE *initSource(const std::string &headerName, const std::string &srcName) {
  yyerror("Opening shader output source.");

}


RtVoid build(const std::string &srcName) {

}
