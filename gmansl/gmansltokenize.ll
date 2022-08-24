/*
 * This is part of the GNU GMAN Library, a FREE implementation of the
 * RenderMan Interface Specification.
 *
 * Copyright (c) 2001, 2000, 1999, Andrew J. Bromage
 *
 * Author: Andrew J. Bromage  <ajb@mail.alicorna.net>
 */

/*
  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.
*/

/* adapted to GMAN by John Cairns <john@2ad.com> */

%{

   #include "gmansl.h"  // local functions and data

   #include "gmanslgrammar.h"

%}

wspace	[ \t\f]+
csymf	[a-zA-Z_]
csym	[a-zA-Z0-9_]
ident	{csymf}({csym})*
uint	[0-9]+
xdigit	[0-9A-Fa-f]
odigit	[0-7]
string	\"(\\.|[^\\"])*\"
floatne	({uint}?\.{uint}|{uint}\.{uint}?)
floate	({floatne}|{uint})[eE][+-]?{uint}
float	({floate}|{floatne})

%state OUTSIDE
%state INLCOMMENT

/* and now the rules... */
%%
			{ BEGIN OUTSIDE; }

<OUTSIDE>light		{ return LIGHT; }
<OUTSIDE>surface	{ return SURFACE; }
<OUTSIDE>volume		{ return VOLUME; }
<OUTSIDE>displacement	{ return DISPLACEMENT; }
<OUTSIDE>transformation	{ return TRANSFORMATION; }
<OUTSIDE>imager		{ return IMAGER; }
<OUTSIDE>extern		{ return EXTERN; }
<OUTSIDE>output		{ return OUTPUT; }
<OUTSIDE>varying	{ return VARYING; }
<OUTSIDE>uniform	{ return UNIFORM; }
<OUTSIDE>void		{ return VOID; }
<OUTSIDE>float		{ return FLOAT; }
<OUTSIDE>string		{ return STRING; }
<OUTSIDE>point		{ return POINT; }
<OUTSIDE>vector		{ return VECTOR; }
<OUTSIDE>normal		{ return NORMAL; }
<OUTSIDE>color		{ return COLOR; }
<OUTSIDE>matrix		{ return MATRIX; }
<OUTSIDE>if		{ return IF; }
<OUTSIDE>else		{ return ELSE; }
<OUTSIDE>break		{ return BREAK; }
<OUTSIDE>continue	{ return CONTINUE; }
<OUTSIDE>return		{ return RETURN; }
<OUTSIDE>while		{ return WHILE; }
<OUTSIDE>for		{ return FOR; }
<OUTSIDE>solar		{ return SOLAR; }
<OUTSIDE>illuminate	{ return ILLUMINATE; }
<OUTSIDE>illuminance	{ return ILLUMINANCE; }
<OUTSIDE>texture	{ return TEXTURE; }
<OUTSIDE>environment	{ return ENVIRONMENT; }
<OUTSIDE>bump		{ return BUMP; }
<OUTSIDE>shadow		{ return SHADOW; }
<OUTSIDE>"+="		{ return PLUSEQ; }
<OUTSIDE>"-="		{ return MINUSEQ; }
<OUTSIDE>"*="		{ return MULTEQ; }
<OUTSIDE>"/="		{ return DIVEQ; }
<OUTSIDE>"=="		{ return EQ; }
<OUTSIDE>"!="		{ return NEQ; }
<OUTSIDE>"<="		{ return LEQ; }
<OUTSIDE>">="		{ return GEQ; }
<OUTSIDE>"&&"		{ return AND; }
<OUTSIDE>"||"		{ return OR; }
<OUTSIDE>"("		{ return '('; }
<OUTSIDE>")"		{ return ')'; }
<OUTSIDE>"{"		{ return '{'; }
<OUTSIDE>"}"		{ return '}'; }
<OUTSIDE>";"		{ return ';'; }
<OUTSIDE>","		{ return ','; }
<OUTSIDE>"="		{ return '='; }
<OUTSIDE>"-"		{ return '-'; }
<OUTSIDE>"."		{ return '.'; }
<OUTSIDE>"*"		{ return '*'; }
<OUTSIDE>"/"		{ return '/'; }
<OUTSIDE>"^"		{ return '^'; }
<OUTSIDE>"+"		{ return '+'; }
<OUTSIDE>"?"		{ return '?'; }
<OUTSIDE>":"		{ return ':'; }
<OUTSIDE>"!"		{ return '!'; }
<OUTSIDE>">"		{ return '>'; }
<OUTSIDE>"<"		{ return '<'; }
<OUTSIDE>"/*"		{ BEGIN INLCOMMENT; }

<OUTSIDE>{uint}		{ return INTCONST; }
<OUTSIDE>{float}	{ return FLOATCONST; }
<OUTSIDE>{string}	{ return STRINGCONST; }

<OUTSIDE>{ident}	{ return IDENTIFIER; }
<OUTSIDE>{wspace}+	;
<OUTSIDE>\n		{ lineNumber++; }
<OUTSIDE>.		{ /* Return error */ }

<INLCOMMENT>"*/"	{ BEGIN OUTSIDE; }
<INLCOMMENT>.		;

%%
