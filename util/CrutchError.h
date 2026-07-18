/* auto-generated (do not modify): type=TEXT creator=KAHL hex=544558544B41484C000000000000000000000000000000000000000000000000 */
//==================================================================================
// CrutchError.h
// ©2024 Steve Crutchfield
//
// Low-level error handling routines to pop up a dialog box, do string wrangling
// with Sprintf(), etc.  Separated here because the error handling routines (Assert
// etc.) in CrutchUtilities.h need these; by breaking them out in a separate file
// we discourage calling Assert etc. from these routines!
//==================================================================================

#ifndef CRUTCHERROR_H
#define CRUTCHERROR_H

#ifdef __cplusplus
extern "C" {
#endif 

#include <stdarg.h>

// ========== Handy defines

typedef Boolean bool;

// UNIQID macro gives an identifier name that is unique within a file+line, beginning
// with the given prefix and an underscore

#define CONCAT_(prefix, suffix) prefix##suffix			// need this indirection to get
#define CONCAT(prefix, suffix)  CONCAT_(prefix, suffix)	// token pasting with __LINE__

#define UNIQID(prefix) 			CONCAT(prefix##_, __LINE__)

// for variables whose name we don't care about and never need to refer to,
// intended for RAII class instances in C++ e.g. 'HLocker anon;'

#define anon UNIQID(_anon)

// ========== STATIC_ASSERT and friends

// STATIC_ASSERT allows for compile time assertions.  the standard way to do this is
// by allocating a negative-size array for cond == 0, but weirdly Sym C++ allows that!
// so we use a zero-size bitfield (note that '!!' preserves zero while turning any
// nonzero value to 1) -- unlike the 'switch (0) { case 0: case 0: ; }' alternative,
// this also allows use in global scope (so we omit a do ... while (0) on purpose):
//
// compile-time error for failed assertion will be 'declarator for 0 sized bitfield'
// (C++) or 'illegal size for bitfield' (THINK C)
//
// in C code (not C++), the first version can only appear at the top of a block.
// 
// the second version is wrapped in a 'sizeof' so that this is an expression, and 
// therefore can appear inside another expression.  the first version isn't, so
// that it can appear on its own without generating a warning.  the third version
// is an expression but discards the value, so can be used e.g. in straight C code
// after the declarations section of a block.

#define _STATIC_ASSERT_TYPE(cond) 	  		struct { int _ : !!(cond); }

#ifndef __cplusplus
// in straight C, can't just define a struct without declaring something; we wrap
// it in an array so that we can declare the same extern array multiple times (e.g.
// if STATIC_ASSERT is used multiple times in a macro --> same "__LINE__") without
// any problems, but prefer the simpler flavor in C++ because it works:
#define STATIC_ASSERT(cond) 	  			extern char anon[sizeof(_STATIC_ASSERT_TYPE(cond))]
#else
#define STATIC_ASSERT(cond) 	  			_STATIC_ASSERT_TYPE(cond)
#endif

#define STATIC_ASSERT_EXPR(cond)  			(sizeof(_STATIC_ASSERT_TYPE(cond)) || true)
#define STATIC_ASSERT_EXPR_DISCARD(cond)  	((void) STATIC_ASSERT_EXPR(cond))

#define	STATIC_ASSERT_IS_SIZEOF_WRAPPED_POINTER(type) \
	STATIC_ASSERT(sizeof(type) == sizeof(void *))

// this just clearly states that we are confirming some object (a typedef, an enum...) 
// is in scope; used in our exceptions macros

#define STATIC_ASSERT_IN_SCOPE(t) 			((void) sizeof(t))

// ========== Other handy defines

#define NOP 0x4E71

#define WITH_A4(statements) \
	do { SetUpA4(); { statements } RestoreA4(); } while (0)

#define IN_SYS_HEAP(statements) 		\
	do { 								\
		const THz savedZone = TheZone; 	\
		TheZone = SysZone; 				\
		{ statements; } 				\
		TheZone = savedZone; 			\
	} while (0)
	
// bitwise 'or' a char with this to convert ucase to lcase:

#define LCASEMASK 0x20

// ========== Error handling

// Guide to my error handling routines

// (all-caps routines are intended as fatal errors and end up calling ABORT, 
// note that in the non-application case this means the SysError will pop up before
// any notification manager alert; we use a DebugStr to at least get useful info
// first but this is crappy UX; basically the all-caps routines should ONLY be used
// in applications!)
//
// ABORT() stops completely:  _ExitToShell (if an application) or _SysError (if not)
//		... except for now, for non-applications this does nothing, some uses of Fail()
//      really aren't emergencies ...
//
// Notify(pStr) displays pStr (without modification) using the notification mgr
//		(for non-applications or if not in front)
//
// Complain(pStr) pops up a dialog-on-the-fly (no resource needed!) to display the message
//		    APP_NAME (a C string) " had a problem—sorry: " pStr
//      if an application in front, else does Notify(pStr)
//      then returns.
//		(recommended usage:  put '#define APP_NAME "..."' in the project prefix)
//
// ComplainSprintf(s, ...) uses format string 's' to format the rest of the arguments
//		and passes the resulting string to Complain()
//
// _Error(...) is a helper function used to concat various preprocessor things and
//      hand them neatly to Complain()
//
// Fail(s) just passes the C string 's' to _Error and ABORTs
//
// Check(e) checks for e==noErr, returning 'true' if so.  Else, it calls Complain()
//      and returns false.  It does not ABORT.
//      Can chain multiple OS calls together with 'if (Check(x || y || ...)) ...'
//
// CheckMesgReturn(e, s, v) is like CheckReturn(e, v) but displays the additional 
//		explanatory message 's'.
//
// CheckReturn(e, v) is like Check(e) but, instead of giving back a value, complains then
//		executes 'return v' if the check fails.  
//		'CheckReturn(e, )' returns void on fail.
//
// CheckFatal(e) does the same as Check, but ABORTs if e!=noErr.
//
// Assert(e) asserts that e is true, else calls Complain() and returns false.  
//		It does not ABORT.
//
// AssertThrows(e, n) (non-C++) is the same as Assert, but does 'throw(n)' on fail.
//
// AssertReturn(e, v) is like Assert(e) but, instead of giving back a value, complains then
//		executes 'return v' if the assertion fails.  'AssertReturn(e, )' returns void on fail.
//
// AssertMesgThrows(e, s, n) (non-C++) is the same as AssertMesg, but does 'throw(n)' on fail.
//
// AssertMesg(e, s) is the same as Assert, but includes explanatory message 's'
//
// AssertMesgReturn(e, s, v) is like AssertMesg(e, s) but, instead of giving back a value,
//      complains then executes 'return v' if the assertion fails.
//      'AssertMesgReturn(e, s, )' or 'AssertMesgReturnVoid(e, s)' returns void on fail.
//
// AssertFatal(e) asserts that e is true, and ABORTs if not.
//
// AssertMesgFatal(e, s) is the same as AssertFatal, but includes explanatory message 's' 
//
// Check can chain multiple OS calls together with ||
// Check and Assert return 'true' if everything OK so can do 
// 	        'if (CHECK(x || y || z...)) ...'
//   	and 'if (ASSERT(x && y && z...)) ...'

#define Fail(s) \
	do { _Error(0, s, __FILE__, __LINE__, true); } while (0)

// helper inlines used by Check macro -- they are set to NOP because the 
// #pragma parameter actually does all the work!

#pragma parameter __D0 _SetD0(__D0)
long _SetD0(long)  = { NOP };  // D0 has been set to parameter already, we're done

#pragma parameter __D0 _GetD0()
long _GetD0(void)  = { NOP };  // compiler will return what's in D0, we're done

// and this one is used by my AssertInline.cpp.in file:
#pragma parameter SetA0(__A0)
void SetA0(void *) = { NOP };  // A0 has been set to parameter already, we're done

// we define Check and CheckFatal this funky way so it can do all of these things:
//   1. only evaluate 'expr' once
//   2. only incur function call overhead if there is an error
//   3. be an expressionm, so we can do if(Check(...)), which precludes using a local
//      variable or an asm block

#define Check(expr)           														\
	(_SetD0(expr) == noErr															\
		|| (_Error(_GetD0(), "when checking ‘" #expr "’", __FILE__, __LINE__, false), false))

#define CheckFatal(expr)           													\
	(_SetD0(expr) == noErr															\
		|| (_Error(_GetD0(), "when checking ‘" #expr "’", __FILE__, __LINE__, true), false))

#define CheckMesg(expr, s)           												\
	(_SetD0(expr) == noErr															\
		|| (_Error(_GetD0(), "when checking ‘" #expr "’—" s, __FILE__, __LINE__, false), false))

#define CheckMesgFatal(expr, s)           											\
	(_SetD0(expr) == noErr															\
		|| (_Error(_GetD0(), "when checking ‘" #expr "’—" s, __FILE__, __LINE__, true), false))

#define CheckReturn(expr, valueToReturnOnFail)										\
	do { 																			\
		const long x = expr;														\
		if (x != noErr) 															\
		{ 																			\
			_Error(x,  "when checking ‘" #expr "’", __FILE__, __LINE__, false); 	\
			return valueToReturnOnFail;												\
		} 																			\
	} while (0)

#define CheckMesgReturn(expr, s, valueToReturnOnFail)								\
	do { 																			\
		const long x = expr;														\
		if (x != noErr) 															\
		{ 																			\
			_Error(x, "when checking ‘" #expr "’—" s, __FILE__, __LINE__, false); 	\
			return valueToReturnOnFail;												\
		} 																			\
	} while (0)

// these two needed from C++, wherein blank arguments for void returns are illegal:

#define CheckReturnVoid(expr)														\
	do { 																			\
		const long x = expr;														\
		if (x != noErr) 															\
		{ 																			\
			_Error(x,  "when checking ‘" #expr "’", __FILE__, __LINE__, false); 	\
			return;																	\
		} 																			\
	} while (0)

#define CheckMesgReturnVoid(expr, s)												\
	do { 																			\
		const long x = expr;														\
		if (x != noErr) 															\
		{ 																			\
			_Error(x, "when checking ‘" #expr "’—" s, __FILE__, __LINE__, false); 	\
			return;																	\
		} 																			\
	} while (0)

#define Assert(expr) \
	((expr) || (_Error(0, "assertion failed: " #expr, __FILE__, __LINE__, false), false))

#ifndef __cplusplus
#define Assert_THROWS(expr, n) \
	do if (!(expr)) { _Error(0, "assertion failed: " #expr, __FILE__, __LINE__, false); throw(n); } while(0)
#endif

#define AssertFatal(expr) \
	((expr) || (_Error(0, "assertion failed: " #expr, __FILE__, __LINE__, true), false))

#define AssertMesg(expr, s) \
	((expr) || (_Error(0, s " (" #expr ")", __FILE__, __LINE__, false), false))

#ifndef __cplusplus
#define AssertMesg_THROWS(expr, s, n) \
	do if (!(expr)) { _Error(0, s " (" #expr ")", __FILE__, __LINE__, false); throw(n); } while(0)
#endif

#define AssertMesgFatal(expr, s) \
	((expr) || (_Error(0, s " (" #expr ")", __FILE__, __LINE__, true), false))

#define AssertReturn(expr, valueToReturnOnFail)										\
	do { 																			\
		if (!(expr)) {																\
			_Error(0, "assertion failed: " #expr, __FILE__, __LINE__, false);	 	\
			return valueToReturnOnFail;												\
		} 																			\
	} while (0)

#define AssertMesgReturn(expr, s, valueToReturnOnFail)								\
	do { 																			\
		if (!(expr)) {																\
			_Error(0, s " (" #expr ")", __FILE__, __LINE__, false); 				\
			return valueToReturnOnFail;												\
		} 																			\
	} while (0)

// these two needed from C++, wherein blank arguments for void returns are illegal:

#define AssertReturnVoid(expr)														\
	do { 																			\
		if (!(expr)) {																\
			_Error(0, "assertion failed: " #expr, __FILE__, __LINE__, false); 		\
			return;																	\
		} 																			\
	} while (0)

#define AssertMesgReturnVoid(expr, s)												\
	do { 																			\
		if (!(expr)) {																\
			_Error(0, s " (" #expr ")", __FILE__, __LINE__, false); 				\
			return;																	\
		} 																			\
	} while (0)

#define NOT_REACHABLE Fail("reached unreachable code")

// ========== ConfirmResource utilities

// confims the resource was loaded -- if purgeable and purged, reload it --
// and verify any specified flags (resSysHeap, resLocked, etc.) are set,
// returning the resource handle
//
// complains if any errors or handle is null
//
// usage:  if (ConfirmResource(GetResource(...))) { ... }

#define ConfirmResource(rsrc) \
	_ConfirmResource((Handle) (rsrc), 0, #rsrc, __FILE__, __LINE__)

#define ConfirmResourceWithFlags(rsrc, flags) \
	_ConfirmResource((Handle) (rsrc), (flags), #rsrc, __FILE__, __LINE__)

Handle _ConfirmResource(Handle rsrc, long flags, char *resStr, char *file, long line);

// ========== Macros for telling who/where we are

#ifndef __cplusplus
	#if __option(a4_globals)
		#define __A4_GLOBALS__
	#endif
#endif

#ifndef __A4_GLOBALS__  // we are an application
	#define APPLICATION
	#define ABORT() ExitToShell()
	#define GL_REG a5
#else
	#define NON_APPLICATION
	#define ABORT() ((void) 0) // or could do .... SysError(3000 + __LINE__)
	#define GL_REG a4
#endif

// get the program counter
// silly note - you can't pass a var named e.g. 'pc' in here because
// it will break the inline asm!
#define GET_PC(here) do { asm { lea 0(pc), here } } while (0)

// is an address, e.g. program counter get GET_PC(), in the application heap zone?
#define ADDR_IS_IN_APPLZONE(addr) ((addr) >= (Ptr) ApplZone && (addr) <= ApplLimit)

// ========== Keymap macros, etc. (used by Trace function)

#define KEYMAP ((long *) 0x174)	// defined in SysEqu.h; usage: KEYMAP[0], KEYMAP[1], etc.

#define KEY_DOWN(k) ((((unsigned char *) KEYMAP) [(k) >> 3] >> ((k) & 7)) & 1)

#define ANY_KEY_DOWN() (                                               	          \
					       KEYMAP[0]  									          \
						| (KEYMAP[1] & ~0x00000002L) /* ignore Caps Lock */       \
						|  KEYMAP[2] 									          \
						|  KEYMAP[3]									          \
					 )

#define ANY_KEY_DOWN_EXCEPT_CMD() (                                    	          \
					       KEYMAP[0]  									          \
						| (KEYMAP[1] & ~0x00008002L) /* ignore Caps Lock & Cmd */ \
						|  KEYMAP[2] 									          \
						|  KEYMAP[3]									          \
					 )

#ifndef __cplusplus
#define BTST_CAPSLOCK 	btst #1, 0x17B  // one-line asm to test for Caps Lock
#endif

// useful char codes

#define ENTER       0x03
#define BACKSPACE	0x08
#define RETURN      0x0D
#define ESC			0x1B
#define LEFT_ARROW  0x1C
#define RIGHT_ARROW 0x1D
#define UP_ARROW    0x1E
#define DN_ARROW    0x1F
#define SPACE		0x20

#define CHECKMARK   18  // e.g. to see if a menu item is checked with GetItemMark()

// useful key codes

#define CMD       0x37
#define OPTION    0x3A
#define OPTION2   0x3D  // right Option key, if configured differently
#define SHIFT     0x38
#define SHIFT2    0x3C  // right Shift key, if configured differently
#define CAPS_LOCK 0x39
#define PAGE_UP   0x74
#define PAGE_DN   0x79
#define LEFT_KEY  0x7B	// Mac II and Extended Keyboard arrows
#define RIGHT_KEY 0x7C
#define UP_KEY	  0x7E
#define DOWN_KEY	  0x7D
#define OLD_LEFT  0x46	// Mac Plus and 1984 keypad arrows
#define OLD_RIGHT 0x42
#define OLD_UP 	  0x4D
#define OLD_DOWN  0x48

// ========== Inline strings
//
// macro to allocate, and get a pointer to, an inline string:
//
// NOTE THIS TRASHES A1
//
// 's' can be a pascal or C string; if a C string, trailing null is omitted 
// (can add with '\0')
//
// 'v' should IDEALLY BE DECLARED AS POINTER TO CONST since modifying *v constitues 
// self-modifying code!  or maybe it doesn't if I don't actually execute these 
// instructions ...
//
// the purpose of this is to avoid using a string constant, which THINK makes 
// A5-relative and so can only be used from code that has an A5 (or A4?) world set up.  
// this way the constant lookup is PC-relative instead and can be referenced at 
// any time.
//
// this adds the cost of a single short branch (the 'bra' below); this could be
// avoided by moving the dc.b line to after a 'return' at end of function
//
// Usage:
//
//		const unsigned char * str;				// declare variable
//		STORE_INLINE_STRING("\pFoo", str);		// then set value with our macro
//
// or just:
//
//		INLINE_STRING(str, "Foo");  			// these both declare and set value
//	or:	INLINE_PSTRING(str, "Foo");				// do NOT use leading '\p' here  

#ifndef __cplusplus
// can't use these from C++ due to inline asm
	
// needs trailing '\0' if a C string:
#define STORE_INLINE_STRING(s, v)    		      			  	\
	{  														 	\
		asm { 			 		 lea    UNIQID(@_data##v), a1 } \
		asm { 			 		 move.l a1, v          		  } \
		asm { 			 		 bra    UNIQID(@_skip##v)     } \
	    asm { UNIQID(@_data##v): dc.b   s       			  } \
		asm { UNIQID(@_skip##v): 			  				  } \
	}

// does NOT need either leading '\p' or trailing '\0':
#define INLINE_STRING(v, s)  const char *v;          STORE_INLINE_STRING(s "\0", v);
#define INLINE_PSTRING(v, s) const unsigned char *v; STORE_INLINE_STRING("\p" s, v);

#endif

// ========== Inline assembly with length calculation
//
// Embeds a chunk of inline asm and get a pointer to it and is length --
// awkwardly, each asm statement in STATEMENTS must be in its own asm 
// block, and all commas must be denoted with 'TO', e.g.:
// 
// long len;
// void *codePtr;
//
// INLINE_ASM( codePtr, len,
// 		asm{ move.l a0 TO d0 } 
//	 	asm{ dc.w 0x1002 }
//	);  // 'codePtr' is now a pointer to the asm code, its length is in 'len'

#define TO ,
#define INLINE_ASM(v, LEN, STATEMENTS) 		\
		asm { lea @_end_data##v, a0 } 		\
		asm { lea @_start_data##v, a1 } 	\
		asm { suba.l a1, a0 } 				\
		asm { move.l a0, LEN } 				\
		asm { move.l @_start_data##v, v } 	\
		asm { bra @_end_data##v } 			\
	@_start_data##v: 						\
		STATEMENTS 							\
	@_end_data##v:
	
// ========== Notification Manager utilities

#define Notify(errorStr) _Notify(errorStr, NULL, NULL)

// the following flavor can be used if from a patched trap that can't move memory 
// (_OSEventAvail) but can access globals -- call this with a pointer to a global NMRec
// and staticStr
//
// NMRec and staticStr should be GLOBAL --
// NOT ALLOCATED ON THE STACK, because must persist until Notification Mgr
// posts the dialog next time GetNextEvent is called:

#define NotifyWithoutMemoryAllocation(errorStr, nmRec, staticStr) \
	_Notify(errorStr, nmRec, staticStr)

void _Notify(ConstStr255Param errorStr, NMRec *nmRec, Str255 staticStringPtr);

// ========== Complain, MessageBox, and _Error

void Complain(ConstStr255Param errorStr);
void ComplainSprintf(const char *s, ...);

void MessageBox(ConstStr255Param s);
void MessageBoxSprintf(const char *s, ...);

// general purpose error handler -- called by Assert and Check macros in CrutchUtilities.h
void _Error(long expr, const char *errStr, const char *file, long line, Boolean abort);

// used to complain if we can't SetUpA4 from C++ -- must do from C so we can have an inline string
void ComplainInliningIsOff(void);

// ========== Sprintf and related functions

void NumToHexString(long n, Str255 s, short nBytes);

unsigned char *va_Sprintf(const char *format, Str255 output, va_list args);

unsigned char *Sprintf(const char *format, Str255 output, ...);

// ========== Debug utilities

#ifdef DEBUG
	void Debug(const char *s, ...);
	void Trace(const char *s, ...);
	
	#define DEBUG_INLINESTR(s) 					\
		{										\
			INLINE_PSTRING(debugStr, s);		\
			DebugStr(debugStr);					\
		}
#else
	#define Debug (void)
	#define Trace (void)
#endif

#define debug Debug
#define trace Trace

// ========== Dialog utilities

// for the common case where we only want a handle to the item
#define GetDItemHandle(dlog, iNum, ptrToHandle) 									\
	do {																			\
		short iType;																\
		Rect r;																		\
		GetDItem((DialogPtr) (dlog), iNum, &iType, (Handle *) (ptrToHandle), &r);	\
	} while (0)

// usage:  load dialog with visible = false, call this function, then ShowWindow()
void SetUpHiliteOK(DialogPtr dlog);

// ========== String utilities

#define CopyStr(s1, s2) \
	do { BlockMove((s1), (s2), (unsigned char) (s1)[0] + 1); } while (0)

void AppendStr(Str255 s, ConstStr255Param suffix);
Boolean EqualStr(ConstStr255Param s1, ConstStr255Param s2);

#ifdef __cplusplus
	void CToPStr(const char * const c, Str255 p);
#else
	void CToPStr(const register char * const c, register Str255 p);
#endif

#define TextHandleToString GetIText  // (GetIText just extracts a Str255 from a sized handle to text)

// ========== Measurement utilities

void CenterRectInRect(Rect * const centerMe, const Rect * const inMe);
void UpperCenterWindow(WindowPtr w);

// ========== Environment utilities

// is it boot time?  seems like at boot time, CurApName starts with 0xFFFFFFFF; 
// anyway an app's name can't be longer than 31 chars so checking for a negative
// length byte seems to work -- I haven't seen this documented anywhere though
#define BOOT_TIME (*((char *) CurApName) < 0)

#define COLOR_QD (ROM85 >= 0x3FFF)  // checks for IM Vol V 8-bit (not 32-bit) Color QD

long SystemVersion(void);  // e.g. 0x00000755 == 7.5.5
Boolean TrapAvailable(short trap);
bool IsDebuggerRunning(void);
Boolean CurrentProcessIsFrontProcess(void);
Boolean WeAreAMultiSegmentCodeResource(void);
Handle GetHandleToThisMultiSegmentCodeResource(void);

// ========== Exceptions

// pull in my C exception-handling system (it lives in its own files, but needs our
// STATIC_ASSERT machinery above, and our Assert_THROWS macros need its throw()):
#include "Exceptions.h"

#ifdef __cplusplus
}
#endif 

#endif
