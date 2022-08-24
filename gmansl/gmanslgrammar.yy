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
 	#include <stdlib.h>  // bison uses free

	#include "gmansl.h" // local functions and data
	
	/* helping yacc be c++ compliant */
	extern int yylex();
	extern void yywrap();
%}


%start source_file

	/* The following token is not real.  That's why it's PSEUDO. */
%nonassoc PSEUDO_LOW_PRECEDENCE

%token IDENTIFIER
%token STRINGCONST
%token FLOATCONST
%token INTCONST

%token LIGHT SURFACE VOLUME DISPLACEMENT TRANSFORMATION IMAGER

%token VARYING UNIFORM OUTPUT EXTERN
%token VOID FLOAT STRING POINT COLOR VECTOR NORMAL MATRIX
%nonassoc IF ELSE
%token BREAK CONTINUE RETURN
%token WHILE FOR SOLAR ILLUMINATE ILLUMINANCE
%token TEXTURE ENVIRONMENT BUMP SHADOW

%token PLUSEQ MINUSEQ MULTEQ DIVEQ
%token EQ NEQ LEQ GEQ
%token AND OR

%%

source_file
	: definitions
	;

definitions
	: /* blank */
	| definitions shader_definition
	| definitions function_definition
	;

shader_definition
	: shader_type IDENTIFIER
	  '(' opt_formals ')'
	  body
	  ;

function_definition
	: opt_type IDENTIFIER
	  '(' opt_formals ')'
	  body
	;

lexical_function_definition
	: opt_externspec /* XXX Not strictly necessary, but removes a
				whole slew of reduce/reduce errors */
	  typespec IDENTIFIER
	  '(' opt_formals ')'
	  body
	;

body :
	  '{' statements '}'
	;

shader_type
	: LIGHT
	| SURFACE
	| VOLUME
	| DISPLACEMENT
	| TRANSFORMATION
	| IMAGER
	;

opt_formals
	: /* blank */
	| formals
	| formals ';'
	;

formals
	: formal_variable_definitions
	| formals ';' formal_variable_definitions
	;

formal_variable_definitions
	: opt_outputspec typespec def_expressions
	;

variable_definitions
	: opt_externspec typespec def_expressions
	;

typespec
	: opt_detail type
	;

def_expressions
	: def_expression
	| def_expressions ',' def_expression
	;

def_expression
	: IDENTIFIER opt_def_init
	;

opt_def_init
	: /* blank */
	| '=' expression
	;

opt_detail
	: /* blank */
	| VARYING
	| UNIFORM
	;

opt_outputspec
	: /* blank */
	| OUTPUT
	;

opt_externspec
	: /* blank */
	| EXTERN
	;

opt_type
	: /* blank */
	| type
	;

type
	: FLOAT
	| STRING
	| COLOR
	| POINT
	| VECTOR
	| NORMAL
	| MATRIX
	| VOID
	;

statements
	: /* blank */
	| statements statement
	| statements variable_definitions ';'
	| statements lexical_function_definition
	;

statement
	: assignexpr ';'
	| procedurecall ';'
	| RETURN expression ';'
	| BREAK opt_integer ';'
	| CONTINUE opt_integer ';'
	| IF relation statement ELSE statement
	| IF relation statement %prec PSEUDO_LOW_PRECEDENCE
	| WHILE relation statement
	| FOR '(' expression ';' relation ';' expression ')' statement
	| SOLAR '(' ')' statement
	| SOLAR '(' expression ',' expression ')' statement
	| ILLUMINATE '(' expression ')' statement
	| ILLUMINATE '(' expression ',' expression ',' expression ')'
		statement
	| ILLUMINANCE '(' expression ')' statement
	| ILLUMINANCE '(' expression ',' expression ',' expression ')'
		statement
	| '{' statements '}'
	;

opt_integer
	: /* blank */
	| INTCONST
	;

primary
	: INTCONST
	| FLOATCONST
	| texture
	| IDENTIFIER
	| IDENTIFIER '[' expression ']'
	| STRINGCONST
	| procedurecall
	| '(' expression ')'
	| '(' expression ',' expression ',' expression ')'
	| '(' expression ',' expression ',' expression ',' expression
	  ',' expression ',' expression ',' expression ',' expression
	  ',' expression ',' expression ',' expression ',' expression
	  ',' expression ',' expression ',' expression ',' expression
	  ')'
	;

unary_expr
	: primary
	| '-' unary_expr
	| typecast unary_expr
	;

dot_expr
	: unary_expr
	| dot_expr '.' unary_expr
	;

multiplicative_expr
	: dot_expr
	| multiplicative_expr '*' dot_expr
	| multiplicative_expr '/' dot_expr
	;

cross_expr
	: multiplicative_expr
	| cross_expr '^' multiplicative_expr
	;

additive_expr
	: cross_expr
	| additive_expr '+' cross_expr
	| additive_expr '-' cross_expr
	;

conditional_expr
	: additive_expr
	| relation '?' additive_expr ':' conditional_expr
	;

assignexpr
	: lhs asgnop assign_expr
	;

asgnop
	: '='
	| PLUSEQ
	| MINUSEQ
	| MULTEQ
	| DIVEQ
	;

lhs
	: IDENTIFIER
	| IDENTIFIER '[' expression ']'
	;

assign_expr
	: conditional_expr
	| assignexpr
	;

expression
	: assign_expr
	;

relation
	: relation2 OR relation
	| relation2
	;

relation2
	: relation3 AND relation2
	| relation3
	;

relation3
	: '!' relation3
	| relation4
	;

relation4
	: '(' relation ')'
	| additive_expr relop additive_expr
	;

relop
	: '>'
	| GEQ
	| '<'
	| LEQ
	| EQ
	| NEQ
	;

procedurecall
	: IDENTIFIER '(' opt_proc_arguments ')'
	;

typecast
	: FLOAT
	| STRING
	| COLOR opt_spacetype
	| POINT opt_spacetype
	| VECTOR opt_spacetype
	| NORMAL opt_spacetype
	| MATRIX opt_spacetype
	;

opt_spacetype
	: /* blank */
	| STRINGCONST
	;

opt_proc_arguments
	: /* blank */
	| proc_arguments
	;

proc_arguments
	: expression
	| proc_arguments ',' expression
	;

texture
	: texture_type '(' texture_filename_with_opt_channel
			opt_texture_arguments ')'
	;

texture_type
	: TEXTURE
	| ENVIRONMENT
	| BUMP
	| SHADOW
	;

texture_filename_with_opt_channel
	: IDENTIFIER '[' expression ']' '[' INTCONST ']'
	| STRINGCONST '[' expression ']'
	| IDENTIFIER '[' expression ']'
		/* Note: This case is ambiguous and needs to be resolved
		   semantically */
	| IDENTIFIER
	| STRINGCONST
	;

opt_texture_arguments
	: /* blank */
	| ',' proc_arguments
	;

%%


