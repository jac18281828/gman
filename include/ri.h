/* This is part of the GNU GMAN Library, a FREE implementation of the
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
 

#ifndef __GMAN_RI_H
#define __GMAN_RI_H 1


/*
 * System specific initialization
 */


#ifdef WIN32
#pragma warning (disable:4251 4275 4305 4786)

namespace std {
	// be sure it exists before the following declaration
};
using namespace std;

// appropriate declarations for various dlls

#ifndef GMANDLL
#ifdef  GMAN_DLL
#define GMANDLL __declspec(dllexport)
#else
#define GMANDLL __declspec(dllimport)
#endif
#endif

// used for dlls in the GMAN project which are not
// the GMAN dll.
//
// these dlls must have separate linkage

#ifndef NOTGMANDLL
#ifdef  NOTGMAN_DLL
#define NOTGMANDLL __declspec(dllexport)
#else
#define NOTGMANDLL __declspec(dllimport)
#endif
#endif

// not win32
#else

#ifndef GMANDLL
#define GMANDLL
#endif

#ifndef NOTGMANDLL
#define NOTGMANDLL
#endif

#endif


/* 
 * RenderMan Interface Standard Include File
 * For ANSI C
 */

/* Global Constants */
/* none */

/* Types Section */
#ifdef __cplusplus
typedef bool	RtBoolean;		/* TRUE/False */
#else
typedef unsigned char RtBoolean;        /* TRUE/False */
#endif
/* Don't assume this is 32bits !!! */
typedef int		RtInt;			/* Integer */
/*
 * By default RtFloats are the size of a float
 * setting this to double might consume excessive memory
 * for some applications, and it doesn't really acomplish
 * very much.  
 *
 * Yes, it would make the renderer a 'true-64bit' render,
 * however this measure of wordsize is just about meaningless.
 * 
 * The real issue is the quality of the lighting model,
 * environment simulation, and number of color samples.
 *
 * Using 64bit values may improve the quality of the Radiosity
 * solution at the expense of time and space, but other techniques
 * such as using more color samples should be used first...
 */
#ifdef __USE_DOUBLE
typedef double		RtFloat;		/* Long Real */
#else
typedef float		RtFloat;		/* Real */
#endif

#define NCOMPS 3

typedef char		*RtToken;	/* a token value string */
typedef RtFloat		RtColor[NCOMPS];	/* RenderMan uses RGB */
typedef RtFloat		RtPoint[3];		/* Three dimensional
						 * spatial coordinate */
typedef RtFloat		RtVector[3];
typedef RtFloat		RtNormal[3];
typedef RtFloat		RtHpoint[4];
/* Standard Matrix */
typedef RtFloat		RtMatrix[4][4];
/* Basis */
typedef RtMatrix        RtBasis;

typedef RtFloat		RtBound[6];		/* An object bounding box */
						/* x1, x2, y1, y2, z1, z2 */
typedef char		*RtString;		/* a char string */

#define RtVoid          void			/* Its not in the K&R API ! */
typedef RtVoid		*RtPointer;		/* a pointer data type */



/* A Function Pointer */
#ifdef __cplusplus
extern "C" {
#endif

typedef RtFloat		(*RtFilterFunc) (RtFloat, RtFloat, RtFloat, RtFloat);
typedef RtVoid		(*RtErrorHandler) (RtInt, RtInt, const char *);

typedef RtVoid          (*RtProcSubdivFunc) (RtPointer, RtFloat);
typedef RtVoid          (*RtProcFreeFunc) (RtPointer);
typedef RtVoid          (*RtArchiveCallback) (RtToken, char *, ...);

#ifdef __cplusplus
}
#endif

typedef RtPointer	RtObjectHandle;		/* Pointer to an internal
						 * object 
						 */
typedef RtPointer	RtLightHandle;		/* pointer to an internal
						 * light object
						 */
typedef RtPointer       RtContextHandle;


/* Extern Declarations for Predefined RI Data Structures */
#ifdef __cplusplus
#define RI_FALSE	false
#else
#define RI_FALSE	0
#endif
#define RI_TRUE		(! RI_FALSE)
#define RI_INFINITY	(RtFloat)1.0e38
#define RI_EPSILON	(RtFloat)1.0e-10
#define RI_NULL		0

/* RIB Interface tokens */

extern RtToken		RI_FRAMEBUFFER, RI_FILE;
extern RtToken		RI_RGB, RI_RGBA, RI_RGBZ, RI_RGBAZ, RI_A, RI_Z, RI_AZ;
extern RtToken		RI_PERSPECTIVE, RI_ORTHOGRAPHIC;
extern RtToken		RI_HIDDEN, RI_PAINT;
extern RtToken		RI_CONSTANT, RI_SMOOTH;
extern RtToken		RI_FLATNESS, RI_FOV;

extern RtToken		RI_AMBIENTLIGHT, RI_POINTLIGHT, RI_DISTANTLIGHT,
			RI_SPOTLIGHT;

extern RtToken		RI_INTENSITY, RI_LIGHTCOLOR, RI_FROM, RI_TO,
			RI_CONEANGLE, RI_CONEDELTAANGLE, RI_BEAMDISTRIBUTION;

extern RtToken		RI_MATTE, RI_METAL, RI_SHINYMETAL,
			RI_PLASTIC, RI_PAINTEDPLASTIC;

extern RtToken		RI_KA, RI_KD, RI_KS, RI_ROUGHNESS, RI_KR,
			RI_TEXTURENAME, RI_SPECULARCOLOR;

extern RtToken		RI_DEPTHCUE, RI_FOG, RI_BUMPY;

extern RtToken		RI_MINDISTANCE, RI_MAXDISTANCE, RI_BACKGROUND,
			RI_DISTANCE, RI_AMPLITUDE;

extern RtToken		RI_RASTER, RI_SCREEN, RI_CAMERA, RI_WORLD, RI_OBJECT;
extern RtToken		RI_INSIDE, RI_OUTSIDE, RI_LH, RI_RH;
extern RtToken		RI_P, RI_PZ, RI_PW, RI_N, RI_NP, RI_CS, RI_OS, RI_S, 
			RI_T, RI_ST;
extern RtToken		RI_BILINEAR, RI_BICUBIC;
extern RtToken		RI_LINEAR, RI_CUBIC;
extern RtToken		RI_PRIMITIVE, RI_INTERSECTION, RI_UNION, RI_DIFFERENCE;
extern RtToken		RI_PERIODIC, RI_NONPERIODIC, RI_CLAMP, RI_BLACK;
extern RtToken		RI_IGNORE, RI_PRINT, RI_ABORT, RI_HANDLER;
extern RtToken          RI_COMMENT, RI_STRUCTURE, RI_VERBATIM;
extern RtToken          RI_IDENTIFIER, RI_NAME, RI_SHADINGGROUP;
extern RtToken          RI_WIDTH, RI_CONSTANTWIDTH;

extern RtToken          RI_CATMULLCLARK;
extern RtToken          RI_HOLE, RI_CREASE, RI_CORNER, RI_INTERPOLATEBOUNDARY;

extern RtToken          RI_ORIGIN;

extern RtBasis		RiBezierBasis, RiBSplineBasis, RiCatmullRomBasis,
			RiHermiteBasis, RiPowerBasis;

#define RI_BEZIERSTEP			((RtInt)3)
#define RI_BSPLINESTEP			((RtInt)1)
#define RI_CATMULLROMSTEP		((RtInt)1)
#define RI_HERMITESTEP			((RtInt)2)
#define RI_POWERSTEP			((RtInt)4)

extern RtInt		RiLastError;


/* RenderMan Interface Subroutines */
#ifdef __cplusplus
extern "C" {
#endif

extern GMANDLL RtFloat		RiGaussianFilter(RtFloat x, RtFloat y,
						 RtFloat xwidth, RtFloat ywidth);

extern GMANDLL RtFloat		RiBoxFilter(RtFloat x, RtFloat y,
				    RtFloat xwidth, RtFloat ywidth);

extern GMANDLL RtFloat		RiTriangleFilter(RtFloat x, RtFloat y,
					 RtFloat xwidth, RtFloat ywidth);

extern GMANDLL RtFloat		RiCatmullRomFilter(RtFloat x,RtFloat y,
					   RtFloat xwidth, RtFloat ywidth);

extern GMANDLL RtFloat		RiSincFilter(RtFloat x, RtFloat y,
				     RtFloat xwidth, RtFloat ywdith);

extern GMANDLL RtVoid		RiErrorIgnore(RtInt code, RtInt severity, const char *msg);
extern GMANDLL RtVoid		RiErrorPrint(RtInt code,  RtInt severity, const char *msg);
extern GMANDLL RtVoid		RiErrorAbort(RtInt code, RtInt  severity, const char *msg);

extern GMANDLL RtVoid           RiProcDelayedReadArchive (RtPointer data, RtFloat detail);
extern GMANDLL RtVoid           RiProcRunProgram (RtPointer data, RtFloat detail);
extern GMANDLL RtVoid           RiProcDynamicLoad (RtPointer data, RtFloat detail);

extern GMANDLL RtContextHandle  RiGetContext (RtVoid);
extern GMANDLL RtVoid           RiContext (RtContextHandle);

extern GMANDLL RtToken		RiDeclare(char *name, char *declaration);

/* Graphics State Machine Control (Push/Pop Stack) */
extern GMANDLL RtVoid
        RiBegin(RtToken name),
        RiEnd(RtVoid),
        RiFrameBegin(RtInt frame),
        RiFrameEnd(RtVoid),
        RiWorldBegin(RtVoid),
        RiWorldEnd(RtVoid);

/* Camera Options */
extern GMANDLL RtVoid
        RiFormat(RtInt xres, RtInt yres, RtFloat aspect),
        RiFrameAspectRatio(RtFloat aspect),
        RiScreenWindow(RtFloat left, RtFloat right, RtFloat bot, RtFloat top),
        RiCropWindow(RtFloat xmin, RtFloat xmax, RtFloat ymin, RtFloat ymax),
        RiProjection(RtToken name, ...),
        RiProjectionV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[]),
        RiClipping(RtFloat hither, RtFloat yon),
        RiDepthOfField (RtFloat fstop, RtFloat focallength, RtFloat focaldistance),
        RiShutter(RtFloat min, RtFloat max);

/* Display Options */
extern GMANDLL RtVoid
        RiPixelVariance(RtFloat variation),
        RiPixelSamples(RtFloat xsamples, RtFloat ysamples),
        RiPixelFilter(RtFilterFunc filterfunc, RtFloat xwidth, RtFloat ywidth),
        RiExposure(RtFloat gain, RtFloat gamma),
        RiImager(RtToken name, ...),
        RiImagerV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[]),
        RiQuantize(RtToken type, RtInt one, RtInt min, RtInt max, RtFloat ampl),
        RiDisplay(char *name, RtToken type, RtToken mode, ...),
        RiDisplayV(char *name, RtToken type, RtToken mode,
		   RtInt n, RtToken tokens[], RtPointer parms[]);

/* Settings for Hidden Surface removal -- If renderer uses it */

extern GMANDLL RtVoid
        RiHider(RtToken type, ...),
        RiHiderV(RtToken type, RtInt n, RtToken tokens[], RtPointer parms[]),
        RiColorSamples(RtInt n, RtFloat nRGB[], RtFloat RGBn[]),
        RiRelativeDetail(RtFloat relativedetail),
        RiOption(RtToken name, ...),
        RiOptionV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[]);

extern GMANDLL RtVoid
        RiAttributeBegin(RtVoid),
        RiAttributeEnd(RtVoid),
        RiColor(RtColor color),
        RiOpacity(RtColor color),
        RiTextureCoordinates(RtFloat s1, RtFloat t1, RtFloat s2, RtFloat t2,
			     RtFloat s3, RtFloat t3, RtFloat s4, RtFloat t4);

extern GMANDLL RtLightHandle
        RiLightSource(RtToken name, ...),
        RiLightSourceV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[]),
        RiAreaLightSource(RtToken name, ...),
        RiAreaLightSourceV(RtToken name,
			   RtInt n, RtToken tokens[], RtPointer parms[]);


extern GMANDLL RtVoid
        RiIllumnate(RtLightHandle light, RtBoolean onoff),
        RiSurface(RtToken name, ...),
        RiSurfaceV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[]),
        RiAtmosphere(RtToken name, ...),
        RiAtmosphereV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[]),
        RiInterior(RtToken name, ...),
        RiInteriorV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[]),
        RiExterior(RtToken name, ...),
        RiExteriorV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[]),
        RiShadingRate(RtFloat size),
        RiShadingInterpolation(RtToken type),
        RiMatte(RtBoolean onoff);

extern GMANDLL RtVoid
        RiBound(RtBound),
        RiDetail(RtBound),
        RiDetailRange(RtFloat minvis, RtFloat lowtran, RtFloat uptran, RtFloat maxvis),
        RiGeometricApproximation(RtToken type, RtFloat value),
        RiOrientation(RtToken orientation),
        RiReverseOrientation(RtVoid),
        RiSides(RtInt sides);

extern GMANDLL RtVoid
        RiIdentity(RtVoid),
        RiTransform(RtMatrix transform),
        RiConcatTransform(RtMatrix transform),
        RiPerspective(RtFloat fov),
        RiTranslate(RtFloat dx, RtFloat dy, RtFloat dz),
        RiRotate(RtFloat angle, RtFloat dx, RtFloat dy, RtFloat dz),
        RiScale(RtFloat sx, RtFloat sy, RtFloat sz),
        RiSkew(RtFloat angle, RtFloat dx1, RtFloat dy1, RtFloat dz1,
	       RtFloat dx2, RtFloat dy2, RtFloat dz2),
        RiDeformation(RtToken name, ...),
        RiDeformationV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[]),
        RiDisplacement(RtToken name, ...),
        RiDisplacementV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[]),
        RiCoordinateSystem(RtToken space),
        RiCoordSysTransform(RtToken space);

/* Spatial transformation of a list of points */
extern GMANDLL RtPoint *
         RiTransformPoints(RtToken fromspace, RtToken tospace, RtInt n,
			   RtPoint points[]);

extern GMANDLL RtVoid
        RiTransformBegin(RtVoid),
        RiTranformEnd(RtVoid);

extern GMANDLL RtVoid
        RiAttribute(RtToken name, ...),
        RiAttributeV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[]);

extern GMANDLL RtVoid
        RiPolygon(RtInt nverts, ...),
        RiPolygonV(RtInt nverts, RtInt n, RtToken tokens[], RtPointer parms[]),
        RiGeneralPolygon(RtInt nloops, RtInt nverts[], ...),
        RiGeneralPolygonV(RtInt nloops, RtInt nverts[], RtInt n,
			  RtToken tokens[], RtPointer parms[]),
        RiPointsPolygons(RtInt npolys, RtInt nverts[], RtInt verts[], ...),
        RiPointsPolygonsV(RtInt npolys, RtInt nverts[], RtInt verts[], RtInt n,
			  RtToken tokens[], RtPointer parms[]),
        RiPointsGeneralPolygons(RtInt npolys, RtInt nloops[], RtInt nverts[],
				RtInt verts[], ...),
        RiPointsGeneralPolygonsV(RtInt npolys, RtInt nloops[], RtInt nverts[],
				 RtInt verts[], RtInt n, RtToken tokens[], 
				 RtPointer parms[]),
        RiBasis(RtBasis ubasis, 
		RtInt ustep, 
		RtBasis vbasis, 
		RtInt vstep),
        RiPatch(RtToken type, ...),
        RiPatchV(RtToken type, RtInt n, RtToken tokens[], RtPointer parms[]),
        RiPatchMesh(RtToken type, RtInt nu, RtToken uwrap,
		    RtInt nv, RtToken vwrap, ...),
        RiPatchMeshV(RtToken type, RtInt nu, RtToken uwrap,
		     RtInt nv, RtToken vwrap, RtInt n, RtToken tokens[], 
		     RtPointer parms[]),
        RiNuPatch(RtInt nu, RtInt uorder, RtFloat uknot[], RtFloat umin,
		  RtFloat umax, RtInt nv, RtInt vorder, RtFloat vknot[],
		  RtFloat vmin, RtFloat vmax, ...),
        RiNuPatchV(RtInt nu, RtInt uorder, RtFloat uknot[], RtFloat umin,
		   RtFloat umax, RtInt nv, RtInt vorder, RtFloat vknot[],
		   RtFloat vmin, RtFloat vmax,
		   RtInt n, RtToken tokens[], RtPointer parms[]),
        RiTrimCurve(RtInt nloops, RtInt ncurves[], RtInt order[],
		    RtFloat knot[], RtFloat min[], RtFloat max[], RtInt n[],
		    RtFloat u[], RtFloat v[], RtFloat w[]);


extern GMANDLL RtVoid
        RiSphere(RtFloat radius, RtFloat zmin, RtFloat zmax, RtFloat tmax, ...),
        RiSphereV(RtFloat radius, RtFloat zmin, RtFloat zmax, RtFloat tmax,
		  RtInt n, RtToken tokens[], RtPointer parms[]),
        RiCone(RtFloat height, RtFloat radius, RtFloat tmax, ...),
        RiConeV(RtFloat height, RtFloat radius, RtFloat tmax,
		RtInt n, RtToken tokens[], RtPointer parms[]),
        RiCylinder(RtFloat radius,RtFloat zmin,RtFloat zmax,RtFloat tmax, ...),
        RiCylinderV(RtFloat radius, RtFloat zmin, RtFloat zmax, RtFloat tmax,
		    RtInt n, RtToken tokens[], RtPointer parms[]),
        RiHyperboloid(RtPoint point1, RtPoint point2, RtFloat tmax, ...),
        RiHyperboloidV(RtPoint point1, RtPoint point2, RtFloat tmax,
		       RtInt n, RtToken tokens[], RtPointer parms[]),
        RiParaboloid(RtFloat rmax,RtFloat zmin,RtFloat zmax,RtFloat tmax, ...),
        RiParaboloidV(RtFloat rmax, RtFloat zmin, RtFloat zmax, RtFloat tmax,
		      RtInt n, RtToken tokens[], RtPointer parms[]),
        RiDisk(RtFloat height, RtFloat radius, RtFloat tmax, ...),
        RiDiskV(RtFloat height, RtFloat radius, RtFloat tmax,
		RtInt n, RtToken tokens[], RtPointer parms[]),
        RiTorus(RtFloat majrad, RtFloat minrad, RtFloat phimin, RtFloat phimax,
		RtFloat tmax, ...),
        RiTorusV(RtFloat majrad,RtFloat minrad,RtFloat phimin,RtFloat phimax,
		 RtFloat tmax, RtInt n, RtToken tokens[], RtPointer parms[]);


extern GMANDLL RtVoid 
        RiBlobby(RtInt nleaf, RtInt ncode, RtInt code[],
		 RtInt nflt, RtFloat flt[],
		 RtInt nstr, RtToken str[], ...),
        RiBlobbyV(RtInt nleaf, RtInt ncode, RtInt code[],
		  RtInt nflt, RtFloat flt[],
		  RtInt nstr, RtToken str[], 
		  RtInt n, RtToken tokens[], RtPointer parms[]);
extern GMANDLL RtVoid
        RiPoints(RtInt npoints, ...),
        RiPointsV(RtInt npoints,
		  RtInt n, RtToken tokens[], RtPointer parms[]),
        RiCurves(RtToken type, RtInt ncurves,
		 RtInt nvertices[], RtToken wrap, ...),
        RiCurvesV(RtToken type, RtInt ncurves,
		  RtInt nvertices[], RtToken wrap,
		  RtInt n, RtToken tokens[], RtPointer parms[]);

extern GMANDLL RtVoid
        RiSubdivisionMesh(RtToken mask, RtInt nf, RtInt nverts[],
			  RtInt verts[],
			  RtInt ntags, RtToken tags[], RtInt numargs[],
			  RtInt intargs[], RtFloat floatargs[], ...),
        RiSubdivisionMeshV(RtToken mask, RtInt nf, RtInt nverts[],
			   RtInt verts[],
			   RtInt ntags, RtToken tags[], RtInt numargs[],
			   RtInt intargs[], RtFloat floatargs[],
			   RtInt n, RtToken tokens[], RtPointer parms[]);

extern GMANDLL RtVoid
        RiProcedural(RtPointer data, RtBound bound,
		     RtVoid (*subdivfunc)(RtPointer, RtFloat),
		     RtVoid (*freefunc)(RtPointer)),
        RiGeometry(RtToken type, ...),
        RiGeometryV(RtToken type, RtInt n, RtToken tokens[], 
		    RtPointer parms[]);

extern GMANDLL RtVoid
        RiSolidBegin(RtToken operation),
        RiSolidEnd(RtVoid) ;

extern GMANDLL RtObjectHandle 
        RiObjectBegin(RtVoid);
extern GMANDLL RtVoid
        RiObjectEnd(RtVoid),
        RiObjectInstance(RtObjectHandle handle),
        RiMotionBegin(RtInt n, ...),
        RiMotionBeginV(RtInt n, RtFloat times[]),
        RiMotionEnd(RtVoid) ;

extern GMANDLL RtVoid 
        RiMakeTexture(char *pic, char *tex, 
		      RtToken swrap, RtToken twrap,
		      RtFilterFunc filterfunc, 
		      RtFloat swidth, RtFloat twidth, ...),
        RiMakeTextureV(char *pic, char *tex, RtToken swrap, RtToken twrap,
		       RtFilterFunc filterfunc, RtFloat swidth, RtFloat twidth,
		       RtInt n, RtToken tokens[], RtPointer parms[]),
        RiMakeBump(char *pic, char *tex, RtToken swrap, RtToken twrap,
		   RtFilterFunc filterfunc, RtFloat swidth, RtFloat twidth, ...),
        RiMakeBumpV(char *pic, char *tex, RtToken swrap, RtToken twrap,
		    RtFilterFunc filterfunc, RtFloat swidth, RtFloat twidth,
		    RtInt n, RtToken tokens[], RtPointer parms[]),
        RiMakeLatLongEnvironment(char *pic, char *tex, RtFilterFunc filterfunc,
				 RtFloat swidth, RtFloat twidth, ...),
        RiMakeLatLongEnvironmentV(char *pic, char *tex, RtFilterFunc filterfunc,
				  RtFloat swidth, RtFloat twidth,
				  RtInt n, RtToken tokens[], RtPointer parms[]),
        RiMakeCubeFaceEnvironment(char *px, char *nx, char *py, char *ny,
				  char *pz, char *nz, char *tex, RtFloat fov,
				  RtFilterFunc filterfunc, RtFloat swidth, 
				  RtFloat ywidth, ...),
        RiMakeCubeFaceEnvironmentV(char *px, char *nx, char *py, char *ny,
				   char *pz, char *nz, char *tex, RtFloat fov,
				   RtFilterFunc filterfunc, RtFloat swidth, 
				   RtFloat ywidth,
				   RtInt n, RtToken tokens[], RtPointer parms[]),
        RiMakeShadow(char *pic, char *tex, ...),
        RiMakeShadowV(char *pic, char *tex,
		      RtInt n, RtToken tokens[], RtPointer parms[]);

extern GMANDLL RtVoid
        RiArchiveRecord(RtToken type, char *format, ...),
        RiReadArchive(RtToken name, RtArchiveCallback callback, ...),
        RiReadArchiveV(RtToken name, RtArchiveCallback callback,
		       RtInt n, RtToken tokens[], RtPointer parms[]);

extern GMANDLL RtVoid
        RiErrorHandler(RtErrorHandler handler);
#ifdef __cplusplus
}
#endif


/*
  Error Codes
  
  1 - 10         System and File Errors
  11 - 20         Program Limitations
  21 - 40         State Errors
  41 - 60         Parameter and Protocol Errors
  61 - 80         Execution Errors
*/

#define RIE_NOERROR     ((RtInt)0)

#define RIE_NOMEM       ((RtInt)1)      /* Out of memory */
#define RIE_SYSTEM      ((RtInt)2)      /* Miscellaneous system error */
#define RIE_NOFILE      ((RtInt)3)      /* File nonexistent */
#define RIE_BADFILE     ((RtInt)4)      /* Bad file format */
#define RIE_VERSION     ((RtInt)5)      /* File version mismatch */
#define RIE_DISKFULL    ((RtInt)6)      /* Target disk is full */

#define RIE_INCAPABLE   ((RtInt)11)     /* Optional RI feature */
#define RIE_UNIMPLEMENT ((RtInt)12)     /* Unimplemented feature */
#define RIE_LIMIT       ((RtInt)13)     /* Arbitrary program limit */
#define RIE_BUG         ((RtInt)14)     /* Probably a bug in renderer */

#define RIE_NOTSTARTED  ((RtInt)23)     /* RiBegin not called */
#define RIE_NESTING     ((RtInt)24)     /* Bad begin-end nesting */
#define RIE_NOTOPTIONS  ((RtInt)25)     /* Invalid state for options */
#define RIE_NOTATTRIBS  ((RtInt)26)     /* Invalid state for attribs */
#define RIE_NOTPRIMS    ((RtInt)27)     /* Invalid state for primitives */
#define RIE_ILLSTATE    ((RtInt)28)     /* Other invalid state */
#define RIE_BADMOTION   ((RtInt)29)     /* Badly formed motion block */
#define RIE_BADSOLID    ((RtInt)30)     /* Badly formed solid block */

#define RIE_BADTOKEN    ((RtInt)41)     /* Invalid token for request */
#define RIE_RANGE       ((RtInt)42)     /* Parameter out of range */
#define RIE_CONSISTENCY ((RtInt)43)     /* Parameters inconsistent */
#define RIE_BADHANDLE   ((RtInt)44)     /* Bad object/light handle */
#define RIE_NOSHADER    ((RtInt)45)     /* Can't load requested shader */
#define RIE_MISSINGDATA ((RtInt)46)     /* Required parameters not provided */
#define RIE_SYNTAX      ((RtInt)47)     /* Declare type syntax error */

#define RIE_MATH        ((RtInt)61)     /* Zerodivide, noninvert matrix, etc. */

/* Error severity levels */

#define RIE_INFO        ((RtInt)0)      /* Rendering stats and other info */
#define RIE_WARNING     ((RtInt)1)      /* Something seems wrong, maybe okay */
#define RIE_ERROR       ((RtInt)2)      /* Problem. Results may be wrong */
#define RIE_SEVERE      ((RtInt)3)      /* So bad you should probably abort */ 


/* End of RenderMan Interface */

#endif
 
