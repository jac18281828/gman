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
#include <iostream>
#if HAVE_STD_NAMESPACE
using std::cerr;
using std::endl;
#endif

/* gman headers */
#include "ri.h"
#include "gmanlog.h"
#include "gmanerror.h"
#include "gmanribparse.h"


/* function prototypes */

RtVoid usage(char *myname);

int main(int argc, char *argv[]) {

  int rc = EXIT_SUCCESS;
  // handle command line arguments
  if(argc < 2) {
    usage(argv[0]);
  } else {
    try {
	   // artificial log object for log settings
      GMANRenderMan   renderMan;

	  GMANLog logObj;
      
      // info on by default
      logObj.setLogLevel(UniversalSuperClass::LOGLVL_INFO);
      // announce the copyright
      logObj.copyright();

      bool writeLog=false;

      int arg=1;

      while((arg < argc) && 
	    (argv[arg][0] == '-')) {
	  switch(argv[arg][1]) {
	      case 'd':
		  logObj.setLogLevel(UniversalSuperClass::LOGLVL_DEBUG);
		  break;
	      case 'e':
		  logObj.setLogLevel(UniversalSuperClass::LOGLVL_ERROR);
		  break;
	      case 'h':
		  usage(argv[0]);
		  exit(EXIT_SUCCESS);
	      case 'i':
		  logObj.setLogLevel(UniversalSuperClass::LOGLVL_INFO);
		  break;
	      case 'l':
		  writeLog=true;
		  break;
	      case 'q':
		  logObj.setLogLevel(UniversalSuperClass::LOGLVL_DISASTER);
		  break;
	      case 'w':
		  logObj.setLogLevel(UniversalSuperClass::LOGLVL_WARNING);
		  break;
	  }
	  arg++;
      }

      for(int i=arg; i<argc; i++) {
	  try {
	      const char *ribFile = argv[i];
	      logObj.info("Parsing %s", argv[i]);
	      
	      if(writeLog) {
		  char *fileName = new char[strlen(ribFile) + 4];
		  strcpy(fileName, ribFile);
		  int dotPos = strlen(fileName)-4;
		  if(fileName[dotPos] == '.') {
		      strcpy(fileName + dotPos, ".log");
		  } else {
		      strcat(fileName, ".log");
		  }
		  logObj.setLogFile(fileName);
		  delete []fileName;
	      }
	      
	      GMANRIBParse	parser(&renderMan,
				       argv[i]);
	      
	      // just parse it...
	      parser.parse();
	      
	  } catch (GMANError &e) {
	      GMANHandleError(e);
	      rc = EXIT_FAILURE;
	  }
      }
    } catch (GMANError &e) {
	GMANHandleError(e);
	rc = EXIT_FAILURE;
    }
  }
  return rc;
}

/* Are you freaking kidding? */
RtVoid usage(char *myname) {
  
  cerr << myname << ": -[hdiweql] files ..." << endl;
  cerr << "\tParse RIB input files." << endl << endl;
  cerr << "\t-h - print this help message." << endl;
  cerr << "\t-d - set logging to: debug output." << endl;
  cerr << "\t-i - set logging to: information output." << endl;
  cerr << "\t-w - set logging to: warning output." << endl;
  cerr << "\t-e - set logging to: error output." << endl;
  cerr << "\t-q - set logging to: quiet, only report disasters." << endl;
  cerr << "\t-l - enable a log based on the filename of the rib." << endl;

}
