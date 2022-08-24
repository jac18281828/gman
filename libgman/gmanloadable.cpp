/* This is part of the GNU GMAN Library, a FREE implementation of the
 * RenderMan Interface Specification.
 *
 * Copyright (c) 2001, 2000, 1999  John Cairns 
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
  FT COLLINS, CO 80525, USA, or write via E-mail john@2ad.com.
*/
/*
 * System Headers
 */
#ifdef WIN32
#include <windows.h>
#else
#ifdef HAVE_LIBDL
#include <dlfcn.h>
#endif
#endif
/* Local Headers */
#include "ri.h"      /* RenderMan Interface */
#include "universalsuperclass.h" /* Super class */
#include "gmanloadable.h" /* Declaration Header */


/*
 * RenderMan API GMANLoadable
 *
 */

const char *GMANLoadable::getInfoFncName = "GMANGetLoadableInfo";

#ifdef WIN32

// default constructor
GMANLoadable::GMANLoadable(const char *path) 
  throw(GMANError) : UniversalSuperClass(),
		     object(NULL),
		     objInfo(NULL)
{ 
    object = LoadLibrary(path);
    
    if(object == NULL) {
	string errorMsg("Unable to open shared object specified by: ");
	    errorMsg.append(path);
	throw(GMANError(RIE_SYSTEM, RIE_SEVERE, errorMsg.c_str()));
    }
    
    LoadInfoFnc getInfo = (LoadInfoFnc) loadSymbol(getInfoFncName);

    if(getInfo == NULL) {
	string errorMsg("Loadable Info function not found: ");
	errorMsg.append(getInfoFncName);
	throw(GMANError(RIE_SYSTEM, RIE_SEVERE, errorMsg.c_str()));
    }
    objInfo = getInfo();
    if(objInfo == NULL) {
	throw(GMANError(RIE_SYSTEM, RIE_SEVERE, "Loadable module missing info data."));
    }

    // output the info 
    info("Loaded Module: %s", objInfo->name);
    info("Author:        %s", objInfo->author);
    info("Copyright:     %s", objInfo->copyright);
    info("Description:   %s", objInfo->description);
};
// default destructor 
GMANLoadable::~GMANLoadable() { 
    if(object) FreeLibrary(object);
};

RtVoid *GMANLoadable::loadSymbol(const char *symName) {
    return GetProcAddress(object, symName);
}

#else
// default constructor
GMANLoadable::GMANLoadable(const char *path) 
  throw(GMANError) : UniversalSuperClass(),
		     object(NULL),
		     objInfo(NULL)
{ 

#ifdef HAVE_LIBDL
  // call dlopen to load the specified path

  object = dlopen(path, RTLD_LAZY);
  
  if(object == NULL) {
    string errorMsg("Unable to open shared object specified by: ");
    errorMsg.append(path);
    errorMsg.append(": ");
    errorMsg.append(dlerror());

    throw(GMANError(RIE_SYSTEM, RIE_SEVERE, errorMsg.c_str()));
  }
  
  LoadInfoFnc getInfo = (LoadInfoFnc) loadSymbol(getInfoFncName);

  if(getInfo == NULL) {
    string errorMsg("Loadable Info function not found: ");
    
    errorMsg.append(getInfoFncName);
    errorMsg.append(": ");
    errorMsg.append(dlerror());

    throw(GMANError(RIE_SYSTEM, RIE_SEVERE, errorMsg.c_str()));

  }

  objInfo = getInfo();

  if(objInfo == NULL) {
    throw(GMANError(RIE_SYSTEM, RIE_SEVERE, "Loadable module missing info data."));
  }
#endif
};


// default destructor 
GMANLoadable::~GMANLoadable() { 
#ifdef HAVE_LIBDL
  if(object) {
    dlclose(object);
  }
#endif
};

RtVoid *GMANLoadable::loadSymbol(const char *symName) {
#ifdef HAVE_LIBDL
  return dlsym(object, symName);
#else
  return NULL;
#endif
}
#endif

// get name of DSO
const char *GMANLoadable::getName(RtVoid) const {
  if((objInfo == NULL) || (objInfo->name == NULL))
    return "Unknown";
  return objInfo->name;
}

// get author of DSO
const char *GMANLoadable::getAuthor(RtVoid) const {
  if((objInfo == NULL) || (objInfo->author == NULL))
    return "Unknown";
  return objInfo->author;
}

// get description of DSO
const char *GMANLoadable::getDescription(RtVoid) const {
  if((objInfo == NULL) || (objInfo->description == NULL))
    return "Unknown";
  return objInfo->description;

}


// get copyright of DSO
const char *GMANLoadable::getCopyright(RtVoid) const {
  if((objInfo == NULL) || (objInfo->copyright == NULL))
    return "Copyright (c) 2001, 2000, 1999 John Cairns, Licenced under the GNU Lesser Public License, http://www.gnu.org";
  return objInfo->copyright;
}


