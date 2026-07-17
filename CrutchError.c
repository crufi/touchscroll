/* auto-generated (do not modify): type=TEXT creator=KAHL hex=544558544B41484C000000000000000000000000000000000000000000000000 */
//==================================================================================
// CrutchError.c
// ©2026 Steve Crutchfield
//
// Low-level error handling routines to pop up a dialog box, do string wrangling
// with Sprintf(), etc.  Separated here because the error handling routines (Assert
// etc.) in CrutchUtilities.h need these; by breaking them out in a separate file
// we discourage calling Assert etc. from these routines!
//
// NOTE - if including in a C++ project ensure the 'C++ compatible' button in THINK
// C Compiler Options is checked (or at least that 'use 4-byte ints' is checked),
// otherwise variadic sprintf int type promotion will be wrong and using the
// %c and %d (or equivalent) escapes in my sprintf will break the function.
//
// INCLUDES STRING CONSTANTS - main segment must be locked & loaded if this file is used
// (string constants end up in the main segment no matter what; main segment can
// be kept very small if this is an issue) -- special purpose routines that need
// string constants when main segment might be unavailable can use my inline string
// mechanism.  Could of course change all constants here and in Assert macros etc.
// to use inline strings, but that feels ... excessive.
//
// - For single-segment code resources, THINK C should automatically use PC-relative
//   string constants, so this shouldn't be a problem (can check __option(pcrel_strings)
//   to confirm this). 
//
// - EXCEPTION may be cdev functions used as Toolbox callbacks (e.g. custom draw item
//   procs), since the cdev resource can move between invocations.  (SetUpHiliteOK()
//   as used in a modal dialog should be fine, since the cdev won't move -- but is
//   reentrant! -- while inside ModalDialog)
//
// Any code in here should do its own error checking and report errors by directly 
// calling _Error() to prevent any infinite recursion.
//==================================================================================

#include <Processes.h>
#include <Traps.h>
#include <GestaltEqu.h>

#include "CrutchError.h"

#ifndef APP_NAME
#error Did you forget '#define APP_NAME "C-string"' in the Prefix pane?
#endif

// ========== static function prototypes

static void _PopUpDialogOnTheFly(ConstStr255Param mesgStr, short iconID, Boolean beep);
static pascal void _FrameDefaultButton(DialogPtr dlog, short userItemNumber);
static void _MessageBox(ConstStr255Param s, short iconID, Boolean beep);

// ========== Notification manager functions

void _Notify(ConstStr255Param s, NMRec *nmRec, Str255 staticStringPtr)
// CORE ERROR HANDLING FUNCTION:  
// Must handle any errors with FAIL; don't call Sprintf, Complain, etc. -- they call us!
//
// two ways to use this:
//
// 1. normal way:  nmRec == NULL
//
//    _Notify() allocates a buffer to hold (1) a simple response cleanup proc (defined
//    with inline machine instructions below), (2) the NMRec that points to the
//    cleanup proc, and (3) the notification alert string.  It then sticks a locked
//    handle to that buffer in the nmRefCon field of the NMRec (which is in the buffer!)
//    and calls NMInstall with a pointer to the buffer.
//
//	  The cleanup proc just calls _NMRemove then HUnlock/HPurges the buffer so the
//    memory manager can kill it later as needed.
//
//    Among other things, this approach ensures we work cleanly:
//
//		- When a cdev posts a notifcation and the main segment has moved by the time
//        it posts
//
//      - When an INIT that fails to load wants to post a notification (so the INIT
//        code including a possible response proc was never detached/locked in the 
//        system heap)
//
//    Hardcoding the machine instructions below prevents the hassle of having to
//    compile this into a separate resource and include it in all my projects.
//
// 2. no-memory-allocation way e.g. to post notification from patched 
//    _OSEventAvail etc:  in that case we must get passed a pointer to a NON-STACK
//    string (e.g. a string global, with A4 set up).
//
{	
	if (!nmRec)  // normal case -- we must allocate the cleanup buffer and string
	{
		typedef struct {
			NMRec 		  nmRec;
			unsigned char data[];  // embedded code from below, followed by Pascal notify string
		} NMCleaner;
		
		const char *cleanUpNMProc;
		const long codeLen;
		NMCleaner **nmCleaner;
		OSErr theErr;

		asm	{
			lea		  @skip, A0
			lea		  @proc, A1
			suba.l	  A1, A0
			move.l	  A0, codeLen	; get length of proc below

			move.l 	  A1, cleanUpNMProc
			bra 	  @skip			; now skip past the rest
			
		 // pascal void _CleanUpNMProc(NMRec *nmRec)
@proc:		movea.l   4(SP), A0		; get NMRec pointer
			dc.w 	  _NMRemove		; dequeue it

			movea.l   NMRec.nmRefCon(A0), A0
			dc.w 	  _HUnlock		; unlock handle from refCon
			dc.w 	  _HPurge		; free handle to NMCleaner

			movea.l   (SP)+, A0		; stash return address
			addq.l    #4, SP		; pop arg from stack
			jmp       (A0)			; return
@skip:	};

		// copy the cleanup proc into an NMCleaner struct in the system heap,
		// include room for code data and notification string, and get a handle to it:
		//
		// (note, _PtrToHand uses THINK glue but _PtrToXHand does not, so to avoid
		// needing A4/A5 set up here, we use _NewHandleSys followed by _PtrToXHand)

		nmCleaner = (NMCleaner **) NewHandleSys(0);
		theErr = PtrToXHand(cleanUpNMProc - sizeof(NMRec), 
						   (Handle) nmCleaner, 
						   sizeof(NMRec) + codeLen + s[0] + 1);

		if (theErr)						  
		{
			SysError(1001);  // out of memory, can't call Complain here so it's DS time
			return;
		}	

		// lock our handle since we need to give _NMInstall a pointer into it:
		
		HLock((Handle) nmCleaner);

		// populate the NMCleaner struct with a notification string and the NMRec fields:
		
		CopyStr(s, (**nmCleaner).data + codeLen);
				
		nmRec = &(**nmCleaner).nmRec;

		nmRec->nmResp	= (ProcPtr) &(**nmCleaner).data;  // point to our machine code
		nmRec->nmStr    = (**nmCleaner).data + codeLen;   // string to follow our asm code
		nmRec->nmRefCon = (long) nmCleaner;  // so our response proc can get/free handle to itself
	}
	else  // no-memory-allocation case; nmRec and staticStringPtr are already valid ptrs
	{
		CopyStr(s, staticStringPtr);

		nmRec->nmResp   = (void *) -1L;  
		nmRec->nmStr    = staticStringPtr;
		nmRec->nmRefCon = 0L;
	}

	nmRec->qType    = nmType;
	nmRec->nmMark   = 0;
	nmRec->nmIcon   = NULL;
	nmRec->nmSound  = NULL;

	if (NMInstall(nmRec) != noErr)
		SysError(1002);  // should never fail
}

// ========== _ConfirmResource

Handle _ConfirmResource(Handle rsrc, long flags, char *resStr, char *file, long line)
// last 3 params are #rsrc and __FILE__ as C strings from the preprocessor, and __LINE__
{
	short err = noErr;
	const Boolean badRes = (err = ResError()) || !rsrc || !*rsrc;
	Boolean badFlags = false;

	if (!badRes && flags)
	{
		// confirm resource attributes (resLocked, resSysHeap, etc.) are as expected
		const short resAttrs = GetResAttrs(rsrc);
		badFlags = ResError() || (resAttrs & flags) != flags;
	}
		
	if (badRes || badFlags)
	{
		if (badRes)
			ComplainSprintf(
				"Couldn't load a resource (error %D) at %s in %s line %L",
				err, resStr, file, line);
		else
			ComplainSprintf(
				"Found wrong resource flags at %s in %s line %L", 
				resStr, file, line);

		return NULL;
	}
	else
		return rsrc;
}

// ========== Dialog box utilities

static void _PopUpDialogOnTheFly(ConstStr255Param mesgStr, short iconID, Boolean beep)
// CORE ERROR HANDLING FUNCTION:
// Can't call Sprintf, Complain, etc. -- they call us!
// Uses no A4/A5 offsets -- OK to report errors even with them not set up
{
	Rect dRect;
	DialogPtr theDialog;
	short itemHit;
	Handle ditl;
	DialogRecord dStorage;  // since we could be called in a low-mem condition?

	// creating a DITL on the fly:
	// I just created the DITL I wanted in ResEdit and copied/pasted the hex here
	// (this obviously wastes ≈50 bytes vs. sticking the hex into a char array but 
	// is just way easier to change with a simple copy/paste from ResEdit)

	const unsigned char null = 0;
	const unsigned char * const nullStr = &null;	
	INLINE_PSTRING(ditlStr, "000200000000008001270094016904024F4B000000000014000A0034002AA002000000000000000A0032007B017588025E30");
		
	dRect.top  		= 80;	// struct initializer would require A4/A5, so do it this way
	dRect.left		= 64;
	dRect.bottom	= 238;
	dRect.right		= 449;
	
	// stuff into a handle; note this handle is released by DisposeDialog() below

	ditl = NewHandle((ditlStr[0] + 1) >> 1);  // prevent rounding down
	
	if (!ditl)
		SysError(1004);  // out of memory
		
	StuffHex(*ditl, ditlStr);
	((short *) (*ditl))[16] = iconID;  // stuff icon ID into the right spot in the ditl

	// put up the dialog
	
	{
		const long a5 = SetCurrentA5();  // so we can draw
		
		theDialog = NewDialog(&dStorage, &dRect, nullStr, false, dBoxProc, (WindowPtr) -1L, false, 0L, ditl);
	
		if (!theDialog) 
			SysError(1005);  // out of memory
	
		UpperCenterWindow(theDialog);		
		ParamText(mesgStr, nullStr, nullStr, nullStr);
		SetUpHiliteOK(theDialog);
		InitCursor();
		ShowWindow(theDialog);
	
		if (beep) 
			SysBeep(1);
		
		ModalDialog(NULL, &itemHit);

		CloseDialog(theDialog);		
		SetA5(a5);
	}	
}

static void _MessageBox(ConstStr255Param s, short iconID, Boolean beep)
// CORE ERROR HANDLING FUNCTION:  
// Can't call Sprintf, Complain, etc. -- they call us!
{
	// could use '#if !__option(a4_globals)' here to check if an app -- however
	// that would prevent using as a precompiled library (in case we ever want to) ...
	// plus it's kind of fun to check on the fly?

	register Ptr here;	
	GET_PC(here);  // grab the program counter

	if (!BOOT_TIME
		&& ADDR_IS_IN_APPLZONE(here)
		&& CurrentProcessIsFrontProcess())
	{
		// our code is running in an application (we are an application not an INIT etc)
		// and our app is in front -- don't use the notification manager, pop up an alert
		// on the fly --
		// this avoid using NM by foreground app (works, but bad practice) but more importantly
		// this interrupts what's happening to show the alert immediately (vs on next WNE)
		// which is the desired behavior:

		_PopUpDialogOnTheFly(s, iconID, beep);
	}
	else
		Notify(s);
}

void MessageBox(ConstStr255Param s)
{
	_MessageBox(s, noteIcon, false);
}

static pascal void _FrameDefaultButton(DialogPtr dlog, short userItemNumber)
// gets default item number from DialogRecord (usually 1)
{
	Handle h;
	short iType;
	Rect r;
	
	GetDItem(dlog, ((DialogPeek) dlog)->aDefItem, &iType, &h, &r);
	InsetRect(&r, -4, -4);
	PenSize(3, 3);
	FrameRoundRect(&r, 16, 16);
	PenNormal();
}

void SetUpHiliteOK(DialogPtr dlog)
// this is pretty nifty:  it adds a user item to the DITL of the given dialog,
// then points its draw proc to hilite the default item.  this way we don't need
// to add user items to all our DITLs in ResEdit and keep track of their item numbers.
//
// (see IM I:427)
//
// NOTE this is a Toolbox callback -- this segment must be locked or it may 
// jump to garbage memory.  In an app or multi-segment code resource (e.g. cdev), 
// as noted above, the CrutchUtilities stuff MUST all be in the main segment 
// (which stays locked).  In a single-segment code resource, there should be no
// issue.
//
// don't do fancy error checking -- Complain() calls us
//
// usage:  load dialog with visible = false, call this function, then ShowWindow()
{
	// create a user item struct, see IM I:427	
	struct DITLUserItem {
		ProcPtr procPtr;
		Rect r;
		short info;
	} u;
	
	// a struct initializer would require A4/A5 to be set up, so do it this way:
	u.procPtr  = _FrameDefaultButton;	
	u.r.top    = u.r.left  = 0;
	u.r.bottom = u.r.right = 8;
	u.info     = 0x8000;		// disabled user item
	
	// append it to the item list handle	
	if (PtrAndHand(&u, ((DialogPeek) dlog)->items, sizeof(struct DITLUserItem)) != noErr)
		SysError(1006);  // out of memory
	
	// increment zero-based item count (the first 2 bytes at 'items')
	++**((short **) ((DialogPeek) dlog)->items);  
}

// ========== Complain and _Error functions

void Complain(ConstStr255Param errorStr)
// CORE ERROR HANDLING FUNCTION:  
// Can't call Sprintf, Complain, etc. -- they call us!
{
	Str255 s;
	INLINE_PSTRING(prefix, APP_NAME " had a problem—sorry:\r\r");
	CopyStr(prefix, s);
	AppendStr(s, errorStr);
	_MessageBox(s, stopIcon, true);
}

void _Error(long errCode, const char *errStr, const char *file, long line, Boolean abort)
// helper function intended to be used by Check, Assert, and Fail macros
//
// 	errStr:  C string (from preprocessor or caller) indicating what caused the error
// 	file:    file name C string from preprocessor
// 	line:    line number from preprocessor
// 	abort:   true if we need to ABORT
{
	Str255 s;

	if (errCode)   // Check case:  got a nonzero error code
	{
		INLINE_STRING(format, "error %L (%s)\rat '%s' line %L");
		Sprintf(format, s, errCode, errStr, file, line);
	}
	else  		   // Assert/Fail case:  no error code to report
	{
		INLINE_STRING(format, "%s\rat '%s' line %L");
		Sprintf(format, s, errStr, file, line);
	}

	Complain(s);
	
	if (IsDebuggerRunning())
	{
#if defined(NON_APPLICATION)
		if (abort)
			// if not an application, DebugStr before ABORT
			// since Complain above uses Notification Mgr which won't get seen yet ....
			DebugStr(s);
		else
		{	
			// not aborting -- DebugStr only if DEBUG is on
			#if defined(DEBUG)
				DebugStr(s);
			#endif
		}
#elif defined(DEBUG)
		// application -- DebugStr only if DEBUG is on
		DebugStr(s);
#endif
	}
	
#ifndef DEBUG  // (in DEBUG case, don't ABORT so we can step back to where the problem occurred and debug)
	if (abort) ABORT();
#endif
}

void ComplainInliningIsOff(void)
// used to complain if we can't SetUpA4 from C++ -- must do from C so we can have an inline string
{
	INLINE_PSTRING(s, "recompile with “use function calls for inlines” off");
	Complain(s);
}

// ========== Sprintf and related functions

// define various functions that apply Sprintf to args and pass to another function 'func':

#define DEFINE_SPRINTF_FUNC(FUNC)			\
	void FUNC##Sprintf(const char *s, ...)	\
	{										\
		Str255 f;							\
		va_list args;						\
											\
		va_start(args, s);					\
		va_Sprintf(s, f, args);				\
		va_end(args);						\
											\
		FUNC(f);							\
	}

DEFINE_SPRINTF_FUNC(MessageBox)  // defines MessageBoxSprintf()
DEFINE_SPRINTF_FUNC(Complain)	 // defines ComplainSprintf()

unsigned char *Sprintf(const char *s, Str255 f, ...)
{
	va_list args;

	va_start(args, f);
	va_Sprintf(s, f, args);
	va_end(args);
	
	return f;
}

unsigned char *va_Sprintf(const char *s, Str255 f, va_list args)
// CORE ERROR HANDLING FUNCTION:  
// Can't call Sprintf, Complain, etc. -- they call us!
//
// 	s: 	 	  a C format string
// 	f: 	 	  a Pascal string to receive the formatted output
// 	va_list:  pointer to the first of a list of variadic args; this allows
//              Sprintf() to be called by other variadic functions (like Debug)
{
	short i, j;

	if (s[0] < 32)
	{
		// first character not printable; if not, feels like a length byte --> Pascal string?
		// do a straight Fail here, can't do Assert since it calls Sprintf...
		INLINE_PSTRING(s, "first argument to Sprintf should be a C string");
		Complain(s);
		f[0] = 0;
		return f;
	}
	
	for (i = 0, j = 1; 
		 s[i] && j < 256; 
		 i++)
	{
		if (s[i] == '%')
		{
			Str255 pStr;
			char *str;
			short k;
			Boolean getAnotherChar, hexFlag = false, longFlag = false;
			long arg;
			
			do {
				getAnotherChar = false;
				
				switch (s[++i])
				{
					case 'l': case 'L':
						longFlag = true;
						// fall thru
					
					// IMPORTANT - see note at top of file about calling this from C++
					
					case 'd': case 'D':  // use when passing in a char, short, or int
					case 'w': case 'W':  // (variadic funcitons use 'default argument
					case 'i': case 'I':  // promotion' so char & short --> sizeof(int)
defaultHex:					
						if (longFlag)
							arg = va_arg(args, long);
						else
							arg = va_arg(args, int);
						
						if (hexFlag)
							NumToHexString(arg, pStr, longFlag ? sizeof(long) : sizeof(int));
						else
						{
							if (s[i] >= 'A' && s[i] <= 'Z') f[j++] = '#';  // capital letter --> show decimal sigil
							NumToString(arg, pStr);
						}
						
						for (k = 1; k <= pStr[0] && j < 256; ) f[j++] = pStr[k++];

						break;
						
					case 'p':  // pascal string
						str = va_arg(args, char *);
						for (k = 1; k <= str[0] && j < 256; ) f[j++] = str[k++];
						break;
					
					case 's': case 'S':  // C string
						str = va_arg(args, char *);
						while (*str && j < 256) f[j++] = *str++;
						break;
				
					case 'r': case 'R':  // Rect (by value, not pointer)
					{
						Rect r = va_arg(args, Rect);
						INLINE_STRING(format, "(%i, %i)-(%i, %i)");
						Sprintf(format, pStr, r.left, r.top, r.right, r.bottom);
						for (k = 1; k <= pStr[0] && j < 256; ) f[j++] = pStr[k++];
						break;
					}
					
					case 'P':  // Point
					{
						Point p = va_arg(args, Point);
						INLINE_STRING(format, "(%i, %i)");
						Sprintf(format, pStr, p.h, p.v);
						for (k = 1; k <= pStr[0] && j < 256; ) f[j++] = pStr[k++];
						break;
					}
					
					case 'c':  // ASCII character
						f[j++] = va_arg(args, int);  // see note above about arg promotion
						break;
					
					case 'C':  // RGBColor (by value, not pointer)
					{
						RGBColor c = va_arg(args, RGBColor);
						INLINE_STRING(format, "(r=%x, g=%x, b=%x)");
						Sprintf(format, pStr, c.red, c.green, c.blue);
						for (k = 1; k <= pStr[0] && j < 256; ) f[j++] = pStr[k++];
						break;
					}
					
					case 't':  // OSType
					{
						const char * t;
						INLINE_STRING(format, "'%c%c%c%c'");
						arg = va_arg(args, long);
						t = (char *) &arg;
						Sprintf(format, pStr, t[0], t[1], t[2], t[3]);
						for (k = 1; k <= pStr[0] && j < 256; ) f[j++] = pStr[k++];
						break;
					}
					
					case 'x': case 'X':  // hex flag, get another char for size specifier
						hexFlag = getAnotherChar = true;
						break;
						
					default:
						if (hexFlag)
						{
							// treat just '%x' as '%xd'
							--i;  // treat next character separately
							goto defaultHex;
						}
						
						{
							INLINE_PSTRING(s, "bad format string in Sprintf");
							Complain(s);
						}
						break;
				}			
			} while (getAnotherChar && j < 256);  // could have incremented j inside loop
		}
		else
			f[j++] = s[i];
	}
	
	f[0] = j - 1;
	return f;
}

void NumToHexString(long n, Str255 s, short nBytes)
// converts a word or long into a string representing its hex value,
// beginning with '$' and put into s
//
// 	nBytes: indicates size of n, must be 2 or 4
{
	char *c = (char *) &s[2];
	unsigned char *b = ((unsigned char *) &n) + (nBytes == 4 ? 0 : 2);
	short i;
	const char *digits;
	
	if (nBytes != 4 && nBytes != 2)
	{
		INLINE_PSTRING(err, "bad nBytes in NumToHexString()");
		Complain(err);
		s[0] = 0;
		return;
	}
	
	s[0] = 1 + (nBytes << 1);
	s[1] = '$';	
	
	// use lookup table because these ASCII values are not contiguous:
	STORE_INLINE_STRING("0123456789ABCDEF", digits);

	while (nBytes--)
	{
		*c++ = digits[*b >> 4];
		*c++ = digits[*b & 0x0F];
		b++;
	}
}

// ========== Debug

#ifdef DEBUG

void Debug(const char *s, ...)
// does NOT preserve trashable regs, maybe it should
//
// if we could use variadic macros, would be nice to have Debug be a macro
// that expands to SetUpDebug() (which does all the work here except actually
// call DebugStr, and leaves the resulting string on the stack) then, separately,
// _DebugStr called from the calling function (so Macsbug will point to code in
// the caller!).  oh well, one day somehow?
{
	va_list args;
	Str255 f;
	
	va_start(args, s);
	va_Sprintf(s, f, args);
	va_end(args);

	// add address of next instruction to execute after whoever called us
	// (note that if called from a utility routine like DebugRect, this will
	// point back there!  Trace however JMPs directly here so this works in
	// that case)
	{
		Str32 callerStr;  // Str32 to save stack space
		register Ptr callerAddr;
		INLINE_STRING(format, " (from %xl)");
		asm { move.l 4(a6), callerAddr }
		Sprintf(format, callerStr, callerAddr);
		AppendStr(f, callerStr);
	}
	
	if (IsDebuggerRunning()) 
		DebugStr(f); 
	else 
	{
		INLINE_STRING(s, APP_NAME " debug message:\r\r%p");
		MessageBoxSprintf(s, f);
	}
}

void Trace(const char *s, ...)
// call Debug() only if Caps Lock is down
// does NOT preserve trashable regs, maybe it should
{
	if (KEY_DOWN(CAPS_LOCK))
		// just jump to Debug() with the same arguments on the stack --
		// this preserves stack return address for Debug() to display
		asm {
			unlk a6
			jmp  Debug  // never comes back
		}
}

#endif

// ========== Measurement utilities

void CenterRectInRect(Rect * const centerMe, const Rect * const inMe)
{
	const short h = centerMe->bottom - centerMe->top;
	const short w = centerMe->right  - centerMe->left;
	
	centerMe->top  = ((inMe->top + inMe->bottom) >> 1) - (h >> 1);
	centerMe->left = ((inMe->left + inMe->right) >> 1) - (w >> 1);

	centerMe->bottom = centerMe->top + h;
	centerMe->right = centerMe->left + w;
}

void UpperCenterWindow(WindowPtr w)
{
	Rect r = w->portRect;
	Rect screen = COLOR_QD ? (**MainDevice).gdRect : screenBits.bounds;
	const short offset = (screen.bottom - screen.top) >> 3;
	
	CenterRectInRect(&r, &screen);
	
	if (r.top - offset < 40) 
		r.top = 40;
	else
		r.top -= offset;
	
	MoveWindow(w, r.left, r.top, false);
}

// ========== Str255 utilities

void AppendStr(Str255 s, ConstStr255Param suffix)
// truncates combined string if would be > 255 chars
// TODO redo in minimal asm for fun
{
	const short cappedSufLen = (s[0] + suffix[0] <= 255) ? suffix[0] : 255 - s[0];
	BlockMove(&suffix[1], &s[s[0] + 1], cappedSufLen);
	s[0] += cappedSufLen;
}

Boolean EqualStr(register ConstStr255Param s1, register ConstStr255Param s2)
// compares two Pascal strings
// TODO redo in minimal asm for fun
{
	register short i;
	
	for (i = 0; i <= s1[0]; i++)
		if (s1[i] != s2[i])
			return false;
	
	return true;
}

void CToPStr(const register char * const c, register Str255 p)
// given a C string in 'c', converts it to a Pascal string in 'p'
// TODO redo in minimal asm for fun
{
	register short i;
	
	for (i = 0; c[i]; i++)
		p[i + 1] = c[i];
	
	p[0] = i;
}

// ========== Environment utilities

long SystemVersion(void)
// e.g. 0x00000755 == 7.5.5
// requires A4/A5 setup because Gestalt calls to THINK glue
{
	long result;
	
	if (Gestalt(gestaltSystemVersion, &result) == noErr)
		return result;
	else
		return 0L;
}

Boolean TrapAvailable(short trap)
// per IM:OS Utilities p. 8-22
{
	const TrapType type = trap & 0x0800 ? ToolTrap : OSTrap;
	
	if (type == ToolTrap)
	{
		// filter cases where older systems mask with 0x01FF rather than 0x03FF -- if it's
		// a high-numbered ToolTrap and 0xAA6E maps to InitGraf (0xA86E), then we are
		// running on an older system and it's definitely not implemented
		if ((trap & 0x03FF) >= 0x0200
					&& GetToolboxTrapAddress(0xAA6E) == GetToolboxTrapAddress(_InitGraf))
			return false;
		else
			return GetToolboxTrapAddress(trap) != GetToolboxTrapAddress(_Unimplemented);
	}
	else
		return GetOSTrapAddress(trap) != GetToolboxTrapAddress(_Unimplemented);
}

const long MacJmp : 0x120;

bool IsDebuggerRunning()
// use debugger flags byte to check if a debugger is running:  see "Macsbug Reference
// and Debugging Guide", p. 369
//
// TODO test this in 32-bit mode --- it works in 24-bit mode
{
//	const long &MacJmp = *((long *) 0x120);
	const char debuggerFlags = GetMMUMode() == true32b ? *((char *) 0xBFF)
							  						   : MacJmp >> 24;
	return (debuggerFlags & 0x20) != 0;
}

Boolean CurrentProcessIsFrontProcess(void)
// is the current process the front process?
// (returns true without crashing if Process Manager doesn't exist [yet])
{
	ProcessSerialNumber thisPSN, frontPSN;
	Boolean sameProcess = false;

	return !TrapAvailable(_OSDispatch)  // running w/o MultiFinder (or Proc Mgr hasn't loaded yet)
		|| (    GetCurrentProcess(&thisPSN) == noErr  // or we are the front process
			 && GetFrontProcess(&frontPSN) == noErr
			 && SameProcess(&thisPSN, &frontPSN, &sameProcess) == noErr
			 && sameProcess);
}

/*
Boolean WeAreAMultiSegmentCodeResource(void)
{
#if __option(a4_globals) && !__option(pcrel_strings)
	// we are a code resource (a4_globals) 
	// but NOT a single-segment code resource (pcrel_strings)
	return true;
#else
	return false;
#endif
}

Handle GetHandleToThisMultiSegmentCodeResource(void)
// are we running in a multi-segment code resource?  if so, return a handle to it
// it, maybe for the caller to lock?
//
// tested, but not currently using anywhere -- but might be handy in Exposé cdev
//
//***TODO isn't this WRONG?  won't A4 always point to the MAIN segment, not 'this'
// segment?
{
	if (WeAreAMultiSegmentCodeResource())
	{
		register Ptr a4_reg;
		Handle codeHndl;
		
		asm { move.l a4, a4_reg }
		
		LoadResource(codeHndl = RecoverHandle(a4_reg));
	
		// LoadResource() was just to confirm we actually got a good handle to
		// a code resource:
		if (ResError() == noErr)
			return codeHndl;
	}
	
	return NULL;
}
*/