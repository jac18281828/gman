/* LJL
 * complete rewrite from John code.
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

#include "gmangetarguments.h"

RtVoid GMANGetArguments(va_list args, RtInt n, RtToken *token, RtPointer *parms)
{
  for(int i=0; i<n; i++) {
    token[i]=va_arg(args,RtToken);
    parms[i]=va_arg(args,RtPointer);
  }
}

RtInt GMANCountArguments (va_list args)
{
  RtToken t;
  RtPointer p;
  int n=0;
  t=va_arg(args,RtToken);
  while (t!=RI_NULL) {
    n++;
    p=va_arg(args,RtPointer);
    t=va_arg(args,RtToken);
  }
  return n;
}
