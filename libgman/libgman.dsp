# Microsoft Developer Studio Project File - Name="libgman" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Dynamic-Link Library" 0x0102

CFG=libgman - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "libgman.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "libgman.mak" CFG="libgman - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "libgman - Win32 Release" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE "libgman - Win32 Debug" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
MTL=midl.exe
RSC=rc.exe

!IF  "$(CFG)" == "libgman - Win32 Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Release"
# PROP BASE Intermediate_Dir "Release"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "Release"
# PROP Intermediate_Dir "Release"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MT /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /YX /FD /c
# ADD CPP /nologo /MT /W3 /GX /O2 /I "..\..\jpeg-6b" /I "..\..\tiff\libtiff" /I "..\..\png" /I "..\..\zlib" /D "NDEBUG" /D VERSION="1.0.0" /D "WIN32" /D "_WINDOWS" /D "HAVE_LIBJPEG" /D "HAVE_LIBTIFF" /D "HAVE_LIBPNG" /D _WIN32_WINNT=0x400 /D "GMAN_DLL" /YX /FD /c
# ADD BASE MTL /nologo /D "NDEBUG" /mktyplib203 /o "NUL" /win32
# ADD MTL /nologo /D "NDEBUG" /mktyplib203 /o "NUL" /win32
# ADD BASE RSC /l 0x409 /d "NDEBUG"
# ADD RSC /l 0x409 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:windows /dll /machine:I386
# ADD LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib jpeg.lib libtiff.lib libpng.lib zlib.lib /nologo /subsystem:windows /dll /machine:I386 /libpath:"..\..\png" /libpath:"..\..\jpeg-6b\Release" /libpath:"..\..\tiff\libtiff" /libpath:"..\..\png\projects\msvc\win32\libpng\lib" /libpath:"..\..\zlib"
# Begin Special Build Tool
SOURCE="$(InputPath)"
PostBuild_Cmds=xcopy/f libgman.lib ..\..\lib	xcopy/f libgman.exp ..\..\lib	xcopy/f libgman.dll ..\..\lib
# End Special Build Tool

!ELSEIF  "$(CFG)" == "libgman - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "Debug"
# PROP BASE Intermediate_Dir "Debug"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "Debug"
# PROP Intermediate_Dir "Debug"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MTd /W3 /Gm /GX /Zi /Od /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /YX /FD /c
# ADD CPP /nologo /MTd /W3 /Gm /GX /ZI /Od /I "../include" /I "..\..\jpeg-6b" /I "..\..\tiff\libtiff" /I "..\..\png" /I "..\..\zlib" /D "_DEBUG" /D "GMAN_DLL" /D "WIN32" /D "_WINDOWS" /D "HAVE_LIBJPEG" /D "HAVE_LIBTIFF" /D "HAVE_LIBPNG" /D _WIN32_WINNT=0x400 /FR /YX /FD /c
# ADD BASE MTL /nologo /D "_DEBUG" /mktyplib203 /o "NUL" /win32
# ADD MTL /nologo /D "_DEBUG" /mktyplib203 /o "NUL" /win32
# ADD BASE RSC /l 0x409 /d "_DEBUG"
# ADD RSC /l 0x409 /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:windows /dll /debug /machine:I386 /pdbtype:sept
# ADD LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib jpeg.lib libtiff.lib libpng.lib zlib.lib /nologo /subsystem:windows /dll /debug /machine:I386 /nodefaultlib:"libc" /out:"Debug/libgmand.dll" /pdbtype:sept /libpath:"..\..\png\projects\msvc\win32\libpng\lib" /libpath:"..\..\jpeg-6b\Release" /libpath:"..\..\tiff\libtiff" /libpath:"..\..\zlib"

!ENDIF 

# Begin Target

# Name "libgman - Win32 Release"
# Name "libgman - Win32 Debug"
# Begin Group "Source"

# PROP Default_Filter "*.cpp"
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmanattributes.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmanbasis.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmanbbox.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmanbitmap.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmanbody.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmanbspworldmanager.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmancapi.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmanclipedge.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmancolor.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmancolorsamples.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmancontext.cpp
# End Source File
# Begin Source File

SOURCE=.\gmandefaults.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmandictionary.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmandisplacementshader.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmanerror.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmanface.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmanfilters.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmanframebuffer.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmangamma.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmangetarguments.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmangraphicstate.cpp
# End Source File
# Begin Source File

SOURCE=.\gmanguard.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmanhpoint.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmanimagershader.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmaninlineparse.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmanisoa.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmanlightsourcemgr.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmanlightsourceshader.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmanlinearworldmanager.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmanloadable.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmanloadablerenderer.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmanloadableshader.cpp
# End Source File
# Begin Source File

SOURCE=.\gmanlog.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmanmatrix4.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmanmutex.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmannoise.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmannormal.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmanobject.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmanobjectmanager.cpp
# End Source File
# Begin Source File

SOURCE=.\gmanoctreeworldmanager.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmanoptions.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmanoutput.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmanoutputjpeg.cpp
# End Source File
# Begin Source File

SOURCE=.\gmanoutputpng.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmanoutputpnm.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmanoutputpolygon.cpp
# End Source File
# Begin Source File

SOURCE=.\gmanoutputtiff.cpp
# End Source File
# Begin Source File

SOURCE=.\gmanoutputwin32.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmanoutputx11.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmanparameterlist.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmanpatchpolyobjectmanager.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmanpoint.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmanpolygon.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmanpolygonclipper.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmanprimitive.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmanprimitives.cpp
# End Source File
# Begin Source File

SOURCE=.\gmanquantize.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmanrenderer.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmanrenderman.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmanribparse.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmanribtokenize.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmansegment.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmanshader.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmanslapi.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmansolidobject.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmansurfaceshader.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmanthread.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmantoken.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmantools.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmantransform.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmantrimcurve.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmanvector.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmanvector4.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmanvertex.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmanvertex4.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmanviewingsystem.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmanvolumeshader.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmanvsorthographic.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmanvsperspective.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmanwindowoutput.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\gmanworldmanager.cpp
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\libgman\universalsuperclass.cpp
# End Source File
# End Group
# Begin Group "Headers"

# PROP Default_Filter "*.h"
# Begin Source File

SOURCE=\projects\qed\gman\include\gmanattributes.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmanbasis.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmanbbox.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmanbitmap.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmanbody.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmanbspworldmanager.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmanclipedge.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmancolor.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmancolorsamples.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmancontext.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmandefaults.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmandictionary.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmandisplacementshader.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmanerror.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmanface.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmanframebuffer.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmangamma.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmangetarguments.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmangraphicstate.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmanguard.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmanhpoint.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmanimagershader.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmaninlineparse.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmanisoa.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmanlightsourcemgr.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmanlightsourceshader.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmanlinearworldmanager.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmanloadable.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmanloadablerenderer.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmanloadableshader.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmanlog.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmanmath.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmanmatrix4.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmanmutex.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmannoise.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmannormal.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmanobject.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmanobjectmanager.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmanoctreeworldmanager.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmanoptions.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmanoutput.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmanoutputjpeg.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmanoutputpng.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmanoutputpnm.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmanoutputpolygon.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmanoutputtiff.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmanoutputwin32.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmanoutputx11.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmanparameterlist.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmanpatchpolyobjectmanager.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmanpoint.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmanpolygon.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmanpolygonclipper.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmanprimitive.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmanprimitives.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmanquantize.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmanreducecolor.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmanrenderer.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmanrenderman.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmanribparse.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmanribtokenize.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmansegment.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmanshader.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmanshaderenvironment.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmanslapi.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmansolidobject.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmansurface.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmansurfaceshader.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmanthread.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmantools.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmantransform.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmantrimcurve.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmantypes.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmanvector.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmanvector4.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmanvertex.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmanvertex4.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmanviewingsystem.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmanvolumeshader.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmanvsorthographic.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmanvsperspective.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmanwindowoutput.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\gmanworldmanager.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\ri.h
# End Source File
# Begin Source File

SOURCE=\projects\qed\gman\include\universalsuperclass.h
# End Source File
# End Group
# End Target
# End Project
