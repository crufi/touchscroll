/* auto-generated (do not modify): type=TEXT creator=KAHL hex=544558544B41484C000000000000000000000000000000000000000000000000 */
//==================================================================================
// CrutchUtilities.h
// ©2024 Steve Crutchfield
//
// A handy library of utilities for INITs, patches, and applications.
// Depends on the lower-level routines in CrutchError.c.
//==================================================================================

#ifndef CRUTCHUTILITIES_H
#define CRUTCHUTILITIES_H

#ifdef __cplusplus
extern "C" {
#endif 

#include <QDOffscreen.h>
#include <Traps.h>  		// for XPRAM trap names, see below
#include <Palettes.h>
#include <Processes.h>		// for ProcessSerialNumber

#include "CrutchError.h"

// ========== Handy defines

#ifndef offsetof
#define offsetof(type, field)  ((size_t) &((type *) 0)->field)  // from stddef.h
#endif

#define ABS(x)    ((x) < 0 ? -(x) : (x))
#define MIN(x, y) ((x) < (y) ? (x) : (y))
#define MAX(x, y) ((x) > (y) ? (x) : (y))

// constants of nature
#define MILLISECONDS_PER_TICK 17L

#define seconds * 60
//#define second  seconds  // conflicts with the 'second' member of pair<>
#define minutes * 3600
#define minute  minutes

// get the first few bytes of a *variable* as a long/short/byte (an lvalue)
// (useful for turning a Point into a long, etc.)
#define FIRST_LONG(x)  (*((long *) &(x)))
#define FIRST_WORD(x)  (*((short *) &(x)))
#define FIRST_BYTE(x)  (*((unsigned char *) &(x)))

// get the contents of given *address* as a long/short/byte (an lvalue)
#define LONG_AT(addr)  (*((long *) (addr)))
#define WORD_AT(addr)  (*((short *) (addr)))
#define BYTE_AT(addr)  (*((unsigned char *) (addr)))

// ========== InitManagers

void InitManagers(ProcPtr resumeProc);  // NULL --> ExitToShell

// ========== QuickDraw utilities

// in an INIT patch, even with A5 set up, if we just use "thePort" we get something A4-
// relative which would be wrong (unless we called InitGraf(&thePort) ourselves, I 
// suppose).  if we are trying to run a patch and use the app's QuickDraw 
// globals/thePort, we need to do this to get thePort:

// first, the following versions are used in CrutchClasses.h since our C++ headers can't
// access LoMem.h -- see note in that file
#define THEPORT_FROM_CURRENTA5_AT(currentA5)    (**(( GrafPtr **) (currentA5)))
#define THECPORT_FROM_CURRENTA5_AT(currentA5)   (**((CGrafPtr **) (currentA5)))
#define SCREENBITS_FROM_CURRENTA5_AT(currentA5) (*((BitMap *) (*((long *) (currentA5)) - 122)))

// now the 'real' versions that rely on the lomem global
#define THEPORT_FROM_CURRENTA5     THEPORT_FROM_CURRENTA5_AT(CurrentA5)
#define THECPORT_FROM_CURRENTA5    THECPORT_FROM_CURRENTA5_AT(CurrentA5)
#define SCREENBITS_FROM_CURRENTA5  SCREENBITS_FROM_CURRENTA5_AT(CurrentA5)

#define theCPort ((CGrafPtr) thePort)  // thePort as a CGrafPtr

// Given 'g' which may be either a GrafPtr or a CGrafPtr, get its portBits or
// portPixMap, respectively.  This obviously could hand back one of two different
// data structures but is useful when we need the baseAddr, rowBytes, or bounds
// which are common to both
//
// (if 2 hi bits of rowBytes are set, we are actually looking at the portVersion field
// of a CGrafPort -- port is a CGrafPtr with a 'portPixMap' that is a PixMapHandle)
//
// NOTE - we can't use this at interrupt time because it double-dereferences a
// handle in the PixMap case
#define GET_GRAFPORT_BITORPIXMAP(g) 						\
	(*(														\
		(((g)->portBits.rowBytes & 0xC000) == 0xC000)		\
			? (BitMap *) (*((CGrafPtr) (g))->portPixMap) 	\
			: &(g)->portBits								\
	))

// execute some statement(s) in global coords:
#define DRAWGLOBAL(statements) do {GrafPtr sp; CGrafPort g; 				\
								   GetPort(&sp); OpenCPort(&g); 			\
								   { statements } 				            \
								   CloseCPort(&g); SetPort(sp);} while (0)

// calc rowbytes for a black & white bitmap of a given width
#define BITS_ROWBYTES(width) (((((width) - 1) >> 4) + 1) << 1)

// LoMem globals for RGB black and white (defined in SysEqu.h) -- I actually can't find
// these documented in IM anywhere, but are listed in "On Macintosh Programming, 
// Advanced Techniques" (Daniel Allen, 1990) and I saw referenced in MacTutor articles 
// from ca. 1987 so probably date back to the original Color QuickDraw.

#define RGBBlackPtr ((RGBColor *) 0xC10)
#define RGBWhitePtr ((RGBColor *) 0xC16)

// set foreground and background colors to black & white

#ifdef __cplusplus
	#define RGB_BW() do {					 \
		if (COLOR_QD)						 \
		{									 \
			RGBForeColor(RGBBlackPtr);		 \
			RGBBackColor(RGBWhitePtr);		 \
		}								     \
	} while (0)
#else
	#define RGB_BW() 						 \
		do 									 \
			if (COLOR_QD)		 			 \
			{ 								 \
				asm { pea  RGBBlackPtr   } 	 \
				asm { dc.w _RGBForeColor } 	 \
				asm { pea  RGBWhitePtr   } 	 \
				asm { dc.w _RGBBackColor } 	 \
			} 								 \
		while (0)
#endif
// save and restore foreground and background colors; note _SaveFore and _SaveBack
// are PM calls documented in IM:VI so not sure how early they became available;
// could always grab CGrafPort fore/back and pmFore/Back color fields instead,
// per TN #259 shoudl then call _PortChanged.
//
// usage:  SAVE_RGB { ... } RESTORE_RGB
//    or:  SAVE_RGB_AND_DO(statements)

#define SAVE_RGB		{{{ ColorSpec _colorSpecFG, _colorSpecBG;							 \
							const Boolean _doSaveColor = COLOR_QD 							 \
								&& (THECPORT_FROM_CURRENTA5->portVersion & 0xC000) == 0xC000;\
																							 \
							if (_doSaveColor) { SaveFore(&_colorSpecFG);					 \
											    SaveBack(&_colorSpecBG); }
							
#define RESTORE_RGB			if (_doSaveColor) { RestoreFore(&_colorSpecFG);					 \
											    RestoreBack(&_colorSpecBG); } }}}

#define SAVE_RGB_AND_DO (statements) \
	do SAVE_RGB {statements} RESTORE_RGB while (0)

short GetFontHeight(void);

short EllipsifyString(const short leftEdge, const short rightEdge, Str255 s);

// a rectangular region is always exactly 10 bytes, so:
#define IS_RECT_RGN(r) (GetHandleSize((Handle) (r)) == 10)

// ========== Resource utilities
	
Handle GetResourceInSysHeap(OSType type, short id);

void GetCurResFileInfo(Str255 name, short *wdRefNum);

Boolean ReloadINITResource(Handle *h, OSType type, short id, Str31 fName, short wdRefNum);

// ========== Debug utilities

#ifdef DEBUG
void DebugRect(const Rect * const r);
#endif 

// ========== Environment information utilities

// has anyone called _TEInit?
#define TEXT_EDIT_AVAILABLE() \
	(GetHandleSize(TEScrpHandle) == TEScrpLength && MemErr == noErr)

short GetVers1(unsigned char *s);

void GetAppInfo(ProcessSerialNumber psn, OSType *type, OSType *creator);

Boolean FindProcess(OSType type, OSType creator, ProcessSerialNumber *psn);

Boolean FrontProcessIs(ProcessSerialNumber psn);

// an undocumented Dialog Manager selector to _DialogDispatch to determine
// if a window is a modal dialog (if yes, returns noErr with nonzero *modalClass)
// see GetWindowModalClass() in System 7.1 code at
// https://github.com/elliotnunn/CubeE/blob/1c8bd4ce45620ccc2df0426a6ba43d31a62f5e37/Toolbox/DialogMgr/DialogDispatch.a#L19
pascal OSErr GetWindowModalClass(WindowPtr w, short *modalClass) 
	= { 0x303C, 0x0402, 0xAA68 };

// ========== More useful defines

// lo-mem globals

#define TICKS     (*((unsigned long *)  0x16A))   // defined in LoMem.h  
#define MOUSE     (*((Point *)          0x830))   // defined in SysEqu.h 
#define THECRSR   (*((Cursor *)         0x844))   // defined in SysEqu.h 

// ========== Sound utilities

short SoundManagerVersion(void);

pascal OSErr GetDefaultOutputVolume(long *level) =
	// put selector code into D0 and call _SoundDispatch 
	// (note selector is wrong in IM:Sound)
	{0x203C, 0x022C, 0x0018, 0xA800};  
	
pascal OSErr SetDefaultOutputVolume(long level) =
	// put selector code into D0 and call _SoundDispatch 
	// (note selector is wrong in IM:Sound)
	{0x203C, 0x0230, 0x0018, 0xA800};
	
// ========== Mini FIFO fixed-length queue

typedef struct FIFO {
	Boolean ownsStorage;  // true --> FIFO_Dispose frees the queue's memory
	short numItems;
	short maxItems;	
	long *items;	
} FIFO;

// bytes needed for caller-supplied FIFO_New storage; the extra byte lets FIFO_New
// even-align an odd buffer pointer (the 68000 requires word alignment)
#define FIFO_BUF_SIZE(maxItems) (sizeof(FIFO) + (maxItems) * sizeof(long) + 1)

FIFO *FIFO_New(short maxItems, void *storage);  // storage:  NULL --> use NewPtr, else FIFO_BUF_SIZE(maxItems) bytes
long FIFO_Oldest(FIFO *q);
long FIFO_Newest(FIFO *q);
void FIFO_Push(FIFO *q, long newItem);
void FIFO_Dispose(FIFO *q);

#define FIFO_GetNumItems(q) ((q)->numItems)

// ========== GrafPort coordinate manipulation

// Gets delta needed to convert between local & global coords, which is the topLeft
// of the portBits.bounds.
//
// This is NOT the same as the "origin" of the local coordinate system relative to the
// topLeft of the portRect (that origin is of course just the topLeft of the portRect,
// and is set by SetOrigin´).
//
// NOTE - not usable at interrupt time because GET_GRAFPORT_BITORPIXMAP isn't
#define GET_GLOBAL_COORD_OFFSET(g) topLeft(GET_GRAFPORT_BITORPIXMAP(g).bounds)

void GlobalToLocalRect(Rect *r, Point offset);
void LocalToGlobalRect(Rect *r, Point offset);

void GlobalToLocalForPort(GrafPtr g, Point *p);
void LocalToGlobalForPort(GrafPtr g, Point *p);

void GlobalToLocalRectForPort(GrafPtr g, Rect *r);
void LocalToGlobalRectForPort(GrafPtr g, Rect *r);

Rect GetGlobalWindowPortRect(WindowPtr w);   // return a window's portRect in global coords
Rect GetGlobalWindowStrucRect(WindowPtr w);  // ... or the (**strucRgn).rgnBBox

// ========== Trap patching
//
//     example usage:
//
//     in global scope:  macro args are trap name, return type, param list
//            DECLARE_PATCH(SystemEvent, short, (EventRecord *e));
//
// 	   in e.g. main() to actually install the patch:
//			  INSTALL_PATCH(Tool [or 'OS'], SystemEvent);

#define DECLARE_PATCH(name, type, args) \
	pascal type (*gOrig##name)##args;   \
	pascal type Patched##name##args

#define INSTALL_PATCH(type, name) do {                          \
		gOrig##name = (void *) Get##type##TrapAddress(_##name); \
		Set##type##TrapAddress((long) &Patched##name, _##name); \
	} while (0)

// just to make it really obvious where in code we call original trap
//
// 		usage:  CALL_ORIG_TRAP(ScrollRect) (&r, dh, dv)
//
#define CALL_ORIG_TRAP(name) gOrig##name

// for stack-based traps:
// can use this at end of a function -- it restoresA4 so the stack is
// same as on entry (can change parameters on stack in meantime and 
// will pass thru to original trap) then jumps to trap and does not 
// return:
//
// *** WARNING: ***
// always check the disassembly when using this to see if
// the JMP is skipping any subsequent register restores -- in particular,
// probably don't use this in a routine that uses register vars --
// adding register vars means THINK C saves/restores registers and
// JMP'ing out of the function skips the register restores!

#define RESTORE_A4_AND_JUMP_TO_TOOLTRAP(trap) 				    	\
	do {															\
		asm { movea.l gOrig##trap (a4), a0 } /* get orig trap  */	\
		asm { move.l  (sp)+, a4			   } /* == RestoreA4() */	\
		asm { unlk 	  a6				   } /* put stack back */   \
		asm { jmp 	  (a0)				   } /* call trap      */	\
	} while (false)

// the following macros intended to be used from inside an asm block
// when A4 not already set up -- for example, in a patch that
// uses no globals except needing original trap address:

// first a helper macro:

#define GET_ORIG_TRAP_IN_A1(trap)													 \
		SetUpA4();								/* (clobbers A1)		  		  */ \
		asm { movea.l gOrig##trap(a4), a1	}	/* save original trap address	  */ \
		asm { move.l  (sp)+, a4  			}	/* == RestoreA4(), put stack back */

// now the jmp/jsr macros themselves:
//
// *** WARNING: ***
// for JMP_ORIG_TRAP, again, always check the disassembly when using to see if
// the JMP is skipping any subsequent register restores -- in particular,
// probably don't use this in a routine that uses register vars --
// adding register vars means THINK C saves/restores registers and
// JMP'ing out of the function skips the register restores!

#define JMP_ORIG_TRAP(trap)						\
	} 											\
	GET_ORIG_TRAP_IN_A1(trap)					\
	asm {										\
		jmp (a1)

#define JSR_ORIG_TRAP(trap)						\
	} 											\
	GET_ORIG_TRAP_IN_A1(trap)					\
	asm {										\
 		jsr (a1)

// ========== Timing utilities

// _Microseconds puts the high 32 bits in A0 and the low 32 bits in D0.  THINK C
// returns long function results in D0, so just declaring it this way (in a non-'pascal'
// function) gets us the low 32 bits in the return value as desired (and if needed,
// caller can still grab the high part in A0 from an asm block)
//
// Note, a bug (?) in Mini vMac can cause this to get permanently stuck in loop
// and back up periodically if called too quickly

#ifdef NEED_EXTRA_TOOLBOX_HEADERS
#define _Microseconds 0xA193
unsigned long Microseconds(void) = { _Microseconds };
#endif

typedef struct Timer {
	TMTask task;  // must be at start of struct
	// ... any private data can go here
} Timer;

// Revised Time Mgr sets high bit of TMTask qType field when timer is running:
#define RUNNING(t) (((t).task.qType & 0x8000) != 0)

void SetUpTimer(Timer *t);
void StartTimer(Timer *t, long count);
void StopTimer(Timer *t);
void CleanUpTimer(Timer *t);

void MiniDelay(long dt);

// ========== Offscreen functions

typedef struct Offscreen {
	Boolean inited;							// was SetUpOffscreen called?
	Boolean calledBeginOffscreen;			// has someone called BeginOffscreen?
	Boolean wasLockedBeforeBeginOffscreen;	// were pixels pre-locked when BeginOffscreen last called?

	CGrafPtr savePort;						// port before BeginOffscreen was last called
	GDHandle saveDevice;					// device before BeginOffscreen was last called

	GWorldPtr g;							// the offscreen world
} Offscreen;

#define pmVersionLocked 1  // see ROM files definition of LockPixels
#define GWORLDPIXELS_LOCKED(g) ((**(g)->portPixMap).pmVersion == pmVersionLocked)

// flags for SetUpOffscreen (powers of 2)
#define USE_SYS_ZONE 1
#define TRY_TEMP_MEM 2
#define PURGEABLE 4

Boolean SetUpOffscreen(Offscreen *o, const Rect * const r, const short depth, const long flags);
void CleanUpOffscreen(Offscreen *o);
void SavePixelsWithOffset(const Offscreen * const o, const Rect * const r, const short dh, const short dv);
void RestorePixelsWithOffset(const Offscreen * const o, const Rect * const r, const short dh, const short dv);
void BlitOffscreenWithOffset(const Offscreen * const oFrom, Offscreen * const oTo, const Rect * const r, short dh, short dv);
void BeginOffscreen(Offscreen * const o);
void EndOffscreen(Offscreen * const o);

#define SavePixels(o, r) 	          SavePixelsWithOffset(o, r, 0, 0)
#define RestorePixels(o, r)           RestorePixelsWithOffset(o, r, 0, 0)
#define BlitOffscreen(from, to, r)    BlitOffscreenWithOffset(from, to, r, 0, 0)

// ========== BTMP resource functions

typedef struct BTMP {
	BitMap bitMap;
	char bits[1];

#ifdef __cplusplus
	private: BTMP() { }
#endif
} BTMP;

void CreateBTMPResourceFromScreen(const Rect * const r, const short id, Str255 resName);

void PlotBTMP(BTMP ** const btmp, const BitMap * dest, const Rect * const r, 
	const short mode, const RgnHandle mask);

void PlotBTMPAtXY(BTMP ** const btmp, const BitMap * dest, const short x, const short y, 
	const short mode, const RgnHandle mask);

// =========== Hide/ShowMenuBar

typedef struct {
	Boolean hidden;
	short oldMBarHeight;
	RgnHandle oldGrayRgn;
} HideMenuBarData;

void HideMenuBar(HideMenuBarData *data, Boolean allowClicks);
void ShowMenuBar(HideMenuBarData *data);

// =========== Saving pixels under a new window
	
Offscreen *SavePixelsUnderWindow(WindowPtr w, long flags);
void RestorePixelsUnderWindow(Offscreen *o);

// macros for convenience -- included braces intentionally to make it tough to accidentfally
// fail to balance these

// usage:  
// 	SAVE_PIXELS_AND_SHOW(newWindow, flags) {
//		... do stuff w/ new window ... 
//		CloseWindow(newW); 
//  } RESTORE_PIXELS

#define SAVE_PIXELS_AND_SHOW(w, flags) {{{ Offscreen *_offsc = SavePixelsUnderWindow(w, flags); \
									  	   ShowWindow(w);

#define RESTORE_PIXELS	 	               RestorePixelsUnderWindow(_offsc); }}}

// =========== XPRAM Traps

#pragma parameter __D0 ReadXPRam(__D0, __D1, __A0)
OSErr ReadXPRam(short numBytes, short whichByte, void *dest) 
		= { 0x4840, 0x3001, _ReadXPRam };
	/* Trap wants numBytes in hi word of D0 and whichByte in lo word, so we do this: */
	/*		0x4840		swap	d0   		*/
	/*		0x3001		move.w	d1, d0		*/
	/*					_ReadXPRam			*/

#pragma parameter __D0 WriteXPRam(__D0, __D1, __A0)
OSErr WriteXPRam(short numBytes, short whichByte, void *src)	
		= { 0x4840, 0x3001, _WriteXPRam }; 		
	/* Trap wants numBytes in hi word of D0 and whichByte in lo word, so we do this: */
	/*		0x4840		swap	d0   		*/
	/*		0x3001		move.w	d1, d0		*/
	/*					_WriteXPRam			*/

// =========== Syntactic sugar for ShowInitIcon (which is linked separately)

bool CallShowInitIcon(short code_id, short icon_id);
bool CallShowInitIconXedOut(short code_id, short icon_id, short x_id);

// =========== Icon utilities routines (in ROM but not in THINK headers v6)

#ifdef NEED_EXTRA_TOOLBOX_HEADERS  // these are defined in the 'Apple #includes' with Sym C++ 7
pascal OSErr PlotIconID(const Rect *theRect, short alignment,
		short transform, short theResID) = {0x303C, 0x0500, 0xABC9};

pascal OSErr PlotIconSuite (const Rect *theRect, short alignment,
		short transform, Handle theSuite) = {0x303C, 0x0603, 0xABC9};  // opcode wrong in Think Ref!
#endif

// =========== AppleEvent utilities

// undocumented traps I found in OSDispatch.c (opcodes in MFPrivate.h)
// which tell various resource-using toolbox calls that we are 'the system'.
// this is needed for sending AppleEvents if foreground app doesn't have
// 'high level event aware' set in its SIZE resource (otherwise AESend() returns
// noPortErr).

pascal OSErr BeginSystemMode(void) = { 0x3F3C, 0x0040, 0xA88F };
pascal OSErr EndSystemMode(void)   = { 0x3F3C, 0x0041, 0xA88F };

// =========== "WDEF code stub trick" stuff
//
// see "The 6-byte Code Resource Stub Trick" in MacTutor vol. 10 no. 1,
// http://preserve.mactech.com/articles/mactech/Vol.10/10.01/Jan94Tips/index.html
//
// Unlike the classic approach which modifies a JMP instruction in code, this stub 
// still includes the function address, but it uses MC68000 instructions to retrieve 
// the address as data, avoiding the code modification problem (which requires added
// complication of flushing cache for later machines).  
//
// The WDEF code resource should just be '2F3A 0004 4E75 0000 0000'. 
// The 32 bits of zero are again the address that will be replaced with a function 
// address. A disassembly might read:
//
// 		MOVE.L *+$0006, -(A7)	; push the DC.L onto the stack for RTS
// 		RTS						; jump to the definition function
// 		DC.L   0 				; placeholder for addr of defn function
//
// Since the DC.L never actually runs as code, there's no risk of confusing the 
// cache in the CPU.  Here's a corresponding C structure:

typedef struct CodeResource10ByteStub
{
	const short  instrs[3];
	void  		*funcPtr;
} CodeResource10ByteStub;

#ifdef __cplusplus
}
#endif 

#endif
