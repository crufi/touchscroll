/* auto-generated (do not modify): type=TEXT creator=KAHL hex=544558544B41484C000000000000000000000000000000000000000000000000 */
//==================================================================================
// TouchScroll.c
// ©2025 Steve Crutchfield
//
// An extension to enable iOS-style swipe-scrolling for System 6 and 7, intended
// for use with Jesús Álvarez' excellent iOS port of Mini vMac, which I use daily
// on my iPad. Includes inertial scrolling and rubberbanding, and adds rubber-band
// indicator on normal (non-swipe) scroll arrow use when at either end of a document, 
// too.
//
// To try it without a touchscreen, just drag the mouse vertically really fast ...
//
// Note that "fast forward"-style emulation (Snow, etc.) that actually accelerates
// the system's wall clock/_Microseconds renders TouchScroll awkward to use, since
// our logic for guessing if something is really a human-muscle-timed swipe gets
// borked in that case. Recommend using at real speed only, or with emulators like
// Mini vMac which just speed up CPU instruction processing without messing with the 
// system clock (a better approach, in my opinion, though Snow is otherwise amazing).
//
// This code was never properly cleaned up and is not intended as a demo of perfect
// '90s coding hygiene (my TidyMenus cdev is better in that respect!). But it works
// very well; I have used it personally for several years now. I hope others may find
// it useful, though concede that "heavy Mini vMac users on iPad" is a pretty small
// audience.
//==================================================================================

//#undef DEBUG

/*
TODO:

- FindControl should do nothing if click was really in a control - see above

- Confirm fixed:

    - STILL doesn't work - bottom side when bounce off bottom
		... see +++ below

WISHLIST:

  * smoother subparital scrolling in drag-scroll case
  * see GrabscrollingrectandDV - do I really ever need 'old school' scrollingrectandddv
    ... can I just always use the NSB one which is more available???
  * wrap it in a CDEV with lots of settings exposed so people can screw around (And
    "defaults" button)
	* (idea - "generic configs cdev" that takes any collection of params and
		builds a UI around them based on data type etc with range validation,
		group boxes, etc ... will never do this but it's a nice idea)
  * Control Mgr Asssumes window top left is 0,0 (see *** below)
    or else people should setorigin.  does that eliminate need for some of this code?
  * include application Skip List
  * horizontal scrolling??  maybe not .... a lot of mechanics, probably little payoff
  * add OptionPageUp in here with a checkbox in cdev
  
- Notes
  * supports vertical scrolling only
  * stop inertial scrolling on an keypress, including e.g. Shift, as a quick 'brake'
    when you see what you're looking for (except Command)
  * when inertially scrolling, hold Command for "turbo mode" to scroll faster -- 
    tap Command repeatedly to accelerate to max speed
  * tries not to activate if Finder in 'by Icon' or 'by Small Icon' view (at least in 7.5.5)
  * not actually a cdev yet despite icon - soon, if people besides me use it?

- Known shortcomings

  * some apps (Script Editor) call NewControl then immediately, for some reason,
    remove the control from the window's controlList!  That's weird, and irreparably
    prevents TouchScroll from working, since we have no way of checking if there
    is a vertical scroll bar in the window when the user starts dragging.  (I guess
    we could keep our OWN list of ControlHandles around based on NewControl calls
    but that seems awfully ad hoc.)
  
  * some apps (ResEdit hex dump views) scroll without using CopyBits or ScrollRect,
    instead (wastefully!) redrawing the entire contents of a window.  This prevents
    the "rubberband" feature from working, since we never get a clear sense of the 
    full area to be scrolled.  (Could assume it's the full window if the scroll bar
    is at the far right edge of a window, though this would be wrong for any apps
    with multiple scrolling panes side-by-side.  For now, we don't do this.)
    
  * some apps (HexEdit) don't set scroll bar contrlValue in the normal way, instead 
    using the contrlValue to represent just the pixel position of the thumb in the 
    scroll bar.  This is a problem with long documents, since you could be scrolled
    partway down while the correct scaled position of the scroll thumb is still at
    pixel zero, fooling TouchScroll into thinking you're still at the stop and
    causing it to "rubberband" if you try to scroll up (even though there really is
    content "above" the visible area).  This is likely not fixable, apps like this
    should probably just go on the skip list once that feature is implemented.
  
  * some apps (THINK Project Manager/SymC++) stop processing update events (uncovered 
  	editor windows may appear blank rather than being filled with text) if very low 
  	on memory - TouchScroll can trigger this circumstance sometimes it seems.  
  	Quitting and restarting the host app fixes it with no harm done.
  
  * see TODO below re THINK Project Manager, also
*/

/*
Here's why we chose this approach:  in short, it's weirdly impossible to know a control's
actual position at an arbitrary time.

	*** IS THIS TRUE?  IM I:314 says control mgr just assumes origin is (0,0) and 
	everyone must call setorigin(0,0) if it's not before calling the control mgr!
	it's likely that controlRects are ALWAYS just relative to (0,0) being topleft!
	does this let me simplify approach here? ***

Apps that scroll the main window may use of of several approaches.  They may leave
the GrafPort alone and just move the drawing.  Or they might use SetOrigin to
move the GrafPort, and use MoveControl to keep the controls in the right place on-
screen (***really???).  Or (and this is the tricky part) they might use SetOrigin to 
move the GrafPort but NOT bother to move the controls, and rely on the fact that they 
only need  (for example) scroll bars' contrlRect to be correct at certain known times, 
e.g. when they call _FindControl or _TrackControl, and do a SetOrigin only right before 
those calls.  (It turns out MacPaint 2.0 does this, among probably many other apps,
because why not?)

The trouble is we don't know which approach an app is using, therefore we have know
way to tell if the contrlRect we see at the time of a _SystemEvent call is actually
in terms of the window's local coordinate system (as determined by its portBits.bounds)
or something else entirely.  Therefore we can't hit-test a control in _SystemEvent,
or know where to fake a mouse-click in a scroll arrow in _OSEventAvail.  *But we DO 
know that a contrlRect is correct when an app calls FindControl*.  So the only 
approach that's guaranteed to work is to have _FindControl (1) do nothing if
the mouse was REALLY clicked (earlier) in a control (check by saving the earlier
theEvent->where) or (2) scroll by returning a fake part code hit if not, and if
gWeAreScrolling is true, based on last mouse move seen by _OSEventAvail patch.

	* Note to self - could this be breaking my expose cdev somehow too? (the setorigin?) 
	I think not but should check ...

THE APPROACH:

- SystemEvent sets gWeAreScrolling
- OSEventAvail determines desired part code and fakes a mouse click (based on fake coord)
	and send mouseup then too
- (no need to patch FindWindow because we only care about swipes in front window anyway)
- FindControl sees our fake click, returns desired part code
- TrackControl gets part code for our fake click and forces mouse point to be right!
*/

#include <SetUpA4.h>

#include <Traps.h>
#include <LoMem.h>
#include <Processes.h>
#include <FixMath.h>
#include <SANE.h>
#include <limits.h>

#define NEED_EXTRA_TOOLBOX_HEADERS // for _Microseconds
#include "CrutchUtilities.h"

// we stuff this in theEvent->message for mouseUp/mouseDown events (which normally
// have no 'message') generated by our _OSEventAvail patch so our _SystemEvent patch
// knows it's us:
#define EVT_MSG_SIGNATURE 'Tüçh'

#define GOOD_INIT_ICON 	-4064
#define X_ICON 			-4063

// app configs

#define BIG_SWIPE_STARTING_INERTIAL_INTERVAL 5

#define TURBO_KEY CMD
#define ANY_KEY_DOWN_EXCEPT_TURBO ANY_KEY_DOWN_EXCEPT_CMD

#define MAX_INERTIAL_TICKS        30L
#define MAX_INERTIAL_MILLISECONDS (MAX_INERTIAL_TICKS * MILLISECONDS_PER_TICK)

#define MIN_INERTIAL_INTERVAL_TO_PARTIALLY_SCROLL 10 // ms
#define MIN_PARTIAL_SCROLL_MILLIS 5

#define TICKS_LAG_TO_REQUIRE_BIGGER_MOUSE_MOVE 15

// total milliseconds to decelerate to zero 
// (e.g., 2000 = always inertially scroll for 2 sec)
#define INERTIAL_DECEL_RATIO 2000

#define kDefaultPixelsPerRow 14

#define PART_CODE_IS_ARROW_OR_PAGE(p) \
	((p) == inUpButton || (p) == inDownButton || (p) == inPageUp || (p) == inPageDown)

typedef pascal void (*ActionProc)(ControlHandle c, short partCode);

// set up trap patches:
//
// declare SystemEvent return type as short instead of Boolean to ensure the
// full 2 bytes expected as SystemEvent return value are on stack;
// this is an idiosyncrasy of SystemEvent; see "Ultimate Mac Programming" 
// by Dave Mark, p. 256

DECLARE_PATCH(SystemEvent,  short, (EventRecord *e));
DECLARE_PATCH(SetCursor,    void,  (CursPtr));
DECLARE_PATCH(ScrollRect,   void,  (const Rect * const r, short dh, short dv, RgnHandle rgn));
DECLARE_PATCH(FindWindow,   short, (Point p, WindowPtr *w));
DECLARE_PATCH(FindControl,  short, (Point p, WindowPtr w, ControlHandle *c));
DECLARE_PATCH(TrackControl, short, (ControlHandle c, Point p, ActionProc action));
DECLARE_PATCH(CopyBits,     void,  (BitMap *src, BitMap *dst, const Rect * const srcRect, const Rect * const dstRect, short mode, RgnHandle mask));

void PostClickFromPartCode(short partCode);

typedef enum {
	kNoOp,
	kScrollSamePartCode,
	kScrollNewPartCode
} MouseCheckerResult;

Boolean ControlIsAReasonableVerticalScrollBar(ControlHandle c);

ControlHandle FindAVerticalScrollBar(WindowPtr w, Point localP);

short DoScrollRectOrCopyBitsStuff(const Rect * const r, short *dv);

MouseCheckerResult MouseChecker(void);

Point GetClickPtFromPartCode(const Rect * const scrollBarRect, const short partCode);

short CalcInertialInterval(long dt, short dv);

void SetInertialVelocityAndDecel(short inertialInterval, 
	Fixed *inertialVelocity, 
	Fixed *inertialDecel);

Boolean StartPartialScrollTimerIfApplicable(Timer *partialScrollTimer, 
	short inertialInterval, long lastPartialScrollDurationMS);

void DoInertialMode(ControlHandle c, ActionProc action, short inertialInterval);

void RestoreScrolledBackPixels(Offscreen *offscreen, Rect *offRect, int scrolledDist, int scrolledAmt);

Boolean GrabScrollingRectAndDV(void);

void UnhiliteControl(ControlHandle c);

WindowPeek FrontNonPaletteWindow(void);

void DoRubberBand(ControlHandle c, Fixed vel, unsigned long lastScrollMics);

void CallActionProc(ControlHandle c, ActionProc action, short partCode);

pascal void NSBScrollBarAction(ControlHandle c, short partCode);

Boolean gWeAreScrolling;
Boolean gDoSimpleScroll;
Boolean gChangedCursor;
ControlHandle gTheScrollBar;
short gPartCode;  // the part code we are "trying" to send to FindControl and TrackControl

Boolean gWeAreInActionProc;
Boolean gGotScrollingRectAndDV;
Rect gScrollingRect; // global coords
short gScrollingDV;
short gPartiallyScrolled;
RgnHandle gJunkRgn;  // updateRgn for ScrollRect, we don't use it
Rect gTempRect;  // used to change srcRect arg on the stack for CopyBits patch

// these globals support "normal scroll bounce" ("NSB") if user just uses a scroll 
// button and hits the ends:

Boolean gNSBEligible;
Boolean gNSBGotScrollingRect;

unsigned long gNSBLastScrollMics;
short gNSBLastFindControlPartCode;
ControlHandle gNSBControl;
Rect gNSBScrollingRect;     // global coords
Rect gNSBControlRect;       // global coords
Rect gNSBLocalControlRect;  // local coords
ActionProc gNSBOrigActionProc;
Boolean gNSBDidntBounceYet;
short gNSBLastTimeSrcRectTop;
short gNSBLastTimeBitsHeight;

Point gLastMouse;

OSType gFrontProcessCreator;
short gThinkCVersion;

Point gClickPt;

unsigned long gLastScrollTicks;
unsigned long gStartScrollTicks;

Cursor gOldCursor;
CursHandle gGrabberCursorHndl;

void StartScrolling(Point where, long when, ControlHandle control, short partCode);
void StopScrolling(void);
void RestoreCursor(void);

void StartScrolling(Point where, long when, ControlHandle control, short partCode)
{
	trace("in startscrolling");
	
	if (!gChangedCursor)
	{
		gOldCursor = THECRSR;
		HLock((Handle) gGrabberCursorHndl);
		CALL_ORIG_TRAP(SetCursor) (*gGrabberCursorHndl);
		gChangedCursor = true;
	}
	
	gTheScrollBar = control;
	gWeAreScrolling = true;
	gLastMouse = where;	
	gStartScrollTicks = when;
	gLastScrollTicks = 0;
	gPartCode = partCode;
	gGotScrollingRectAndDV = false;
	gScrollingDV = 0;

	PostClickFromPartCode(partCode);
}

void RestoreCursor(void)
{
	if (gChangedCursor)
	{	
		CALL_ORIG_TRAP(SetCursor) (&gOldCursor);
		HUnlock((Handle) gGrabberCursorHndl);
		gChangedCursor = false;
	}
}

void StopScrolling(void)
{
	trace("in stopscrolling");
	
	gWeAreScrolling = false;
	gPartCode = 0;
	gGotScrollingRectAndDV = false;
	gScrollingDV = 0;

	RestoreCursor();
	ShowCursor();  // in case we called _ObscureCursor earlier
}

Boolean ControlIsAReasonableVerticalScrollBar(ControlHandle c)
// given a control, is it plausibly a vertical scroll bar we might automate here?
{
	if ( c && *c
		&& (**c).contrlVis
	 	&& (**c).contrlMin < (**c).contrlMax
	 	&& (**c).contrlRect.right - (**c).contrlRect.left == 16  // vertical scroll bar
	 	&& (**c).contrlRect.bottom - (**c).contrlRect.top > 60   // no weird tiny weird vertical scroll bars please
	 	&& (**c).contrlHilite == 0)  // not inactive (255) or in some weird hilite state
	{
		short resID;
		ResType resType;
		Str255 resName;  // could just pass a NULL in since 1986, but maybe we want to run on a 512k one day????
		short resError;
		
		// StripAddress here is necessary!  (there could be data in the high bits)
		GetResInfo((Handle) StripAddress((**c).contrlDefProc), &resID, &resType, resName);
		return !ResError() && resID == 1;
	}
	
	return false;
}

ControlHandle FindAVerticalScrollBar(WindowPtr w, Point localP)
// we need to figure out which scroll bar a swipe is supposed to control!
//
// look for a vertical scroll bar in the front window and appropriately
// positioned relative to the mouse
//
// return NULL if mouse is directly on any control as it's clicked 
{
	ControlHandle c, foundScrollBar = NULL;
	short horizDist = SHRT_MAX;
	
	// de-SetOrigin-ify this point for comparison to a control's contrlRect
	// (see IM I:143, Control Manager expects window's local origin to be (0, 0))
	
	Point adjLocalP = localP;
	adjLocalP.h -= w->portRect.left;
	adjLocalP.v -= w->portRect.top;
	
	for (c = ((WindowPeek) w)->controlList; c; c = (**c).nextControl)
	{
		if (PtInRect(adjLocalP, &(**c).contrlRect))
			return NULL;  // mouse in a control
		else if (ControlIsAReasonableVerticalScrollBar(c))
		{
			Rect cRect = (**c).contrlRect;
			const short newHorizDist = cRect.left - adjLocalP.h;
			trace("looking at contrlRect %r vs adjlocalpt %P", (**c).contrlRect, adjLocalP);

			// grab the first scroll bar we see that's to the right of the mouse
			// and where the mouse Y-coord is between the controls top/bottom; note
			// this isn't perfect because we aren't certain contrlRect is correct
			// in local coords at this time (see note at top) 
 			//
 			// once we have one, only replace it if the next one is horizontally 
 			// closer to the mouse than the prior
 			 
			if (newHorizDist >= 0
				&& adjLocalP.v >= cRect.top 
				&& adjLocalP.v <= cRect.bottom
				&& (!foundScrollBar || newHorizDist < horizDist))
			{
				foundScrollBar = c;  // but continue looping to ensure mouse isn't in some subsequent control
				horizDist = newHorizDist >= 0 ? newHorizDist : SHRT_MAX;
			}
		}
	}
	
	return foundScrollBar;
}

WindowPeek FrontNonPaletteWindow(void)
// get the frontmost window with a title bar that is at least 12 pixels high
// (MacPaint tool palettes have title bars that are 11 pixels high)
{
	WindowPeek w = (WindowPeek) FrontWindow();
	
	while(w && (**w->contRgn).rgnBBox.top - (**w->strucRgn).rgnBBox.top < 12)
	{
		trace("floating palette?  getting next window");
		w = w->nextWindow;
	}
	
	return w;
}

pascal short PatchedSystemEvent(EventRecord *e)
// the point of this patch is to determine if we started a fast drag (i.e. possible 
// swipe): eat the mouseDown event if so, and set up global vars for our Time Mgr task to 
// begin scrolling
{
	Boolean weProcessedIt = false;
	ProcessSerialNumber frontProcess;
	WindowPeek frontW;
	
	SetUpA4();

	// analyze the event

	if (e->what == mouseUp
		&& e->message != EVT_MSG_SIGNATURE		
		)
	{
		// a mouseUp that didn't come from our OSEventAvail patch -- clean up
		
		// need this even if not scrolling to not keep old part code around for when
		// we need to use our nonstandard action proc on a regular scroll so we can
		// do rubberbanding:
		gNSBLastFindControlPartCode = 0;
		gNSBLastTimeSrcRectTop = 0;
		gNSBLastTimeBitsHeight = 0;
		
		if (gWeAreScrolling)
			StopScrolling();
	}
	else if (e->what == mouseDown
		&& !gWeAreScrolling 
		&& StillDown()
		&& e->message != EVT_MSG_SIGNATURE
		&& (frontW = FrontNonPaletteWindow())
		&& frontW->controlList
		&& GetFrontProcess(&frontProcess) == noErr)
	{
		ControlHandle testCtl, control = NULL;
		Rect portRect = ((WindowPtr) frontW)->portRect;
		Boolean appIsInSkipList = false;
		OSType processType, processCreator;
		
		Point localP = e->where;
		GlobalToLocalForPort((GrafPtr) frontW, &localP);

		GetAppInfo(frontProcess, &processType, &processCreator);
		
		// if we switched into THINK C, check the version to enable our scroll bar
		// hack below
		
		if (processCreator == 'KAHL' && processCreator != gFrontProcessCreator)
			gThinkCVersion = GetVers1(NULL);
		
		gFrontProcessCreator = processCreator;
		
		// don't activate if in Finder with 'by Small Icon' or 'by Icon' checked in View menu

		if (processType == 'FNDR' && gFrontProcessCreator == 'MACS')
		{
			const MenuHandle viewMenu = GetMHandle(259);  // ID 259 is view menu in 7.5.5 at leasts
			short mark;

			if (viewMenu 
				&& (  (GetItemMark(viewMenu, 1, &mark), mark == CHECKMARK)
				   || (GetItemMark(viewMenu, 2, &mark), mark == CHECKMARK)))
			{
				appIsInSkipList = true;
			}
		}
						
		if (!appIsInSkipList
			&& PtInRect(localP, &portRect))  // mouse is in frontW
		{
			control = FindAVerticalScrollBar((WindowPtr) frontW, localP);

			if (control)
			{
				short dh, dv;

				while (TICKS < e->when + 3)  // give user a chance to get the mouse moving
				{
					// (note - tempting to want to do _HideCursor or _ShieldCursor here,
					// but on a fast swipe cursor has already moved by the time we get
					// here -- and hiding now would result in flicker on every click,'
					// so we leave it alone and accept a little inevitable visual lag
					// of changing to "grabber" cursor below)
					
					if (!gWeAreScrolling && !StillDown())  // see note above
						goto done;  // was it just a click?  don't wait around
				}
				
				// if we're still here, mouse is still down after delay -- did it move?
				
				dh = MOUSE.h - e->where.h;
				dv = MOUSE.v - e->where.v;
						
				if (dv != 0
					&& (dv > 0 ? dv : -dv) >= (dh > 0 ? dh : -dh)  // mouse moved more vertically than horizontally
					)
				{
					// start scrolling!
					StartScrolling(e->where, e->when, control, dv > 0 ? inUpButton : inDownButton);
					weProcessedIt = true;
				}
				else if (gFrontProcessCreator == 'DanR'  // THINK Reference creator code
					&& EqualStr((**control).contrlTitle, "\pvert scroll bar"))
				{
					// special case for THINK Reference to prevent it from getting
					// control if we hold the mouse down and move it slowly or sideways --
					// don't want its weird autoscroll behavior so we just keep control here
					// until the mouse is up!  (check control title to match main window scroll
					// bar so anything else in THINK Ref, e.g. SFGetFile scroll bar, works normally)
					
					while (Button()) {}
					
					if (TICKS - e->when > 20)
						// let short clicks through for clicking links, popup menus, etc.
						// but block longish clicks (which look like an aborted attempt to 
						// scroll)
						weProcessedIt = true;
				}
			}
			else
				trace("no vScrollBar or mouse is in control, not scrolling");
		}
	}
	
done:
	if (weProcessedIt)
	{
		// eat the mouseDown
		e->what = nullEvent;
		RestoreA4();
		return false;
	}
	else
		RESTORE_A4_AND_JUMP_TO_TOOLTRAP(SystemEvent);

	// never get here
	Fail("should never get here");
}

MouseCheckerResult MouseChecker(void)
// do periodic mouse location check stuff inside TrackControl loop
// return TRUE if part code needs to change (exit and re-call TrackControl)
//
// SIDE EFFECT - changes gPartCode to correspond to that mouse movement
{
	Point p = MOUSE;
	MouseCheckerResult result = kNoOp;

	Boolean mouseMovedEnough;
	const short dv = p.v - gLastMouse.v;

	if (TICKS - gLastScrollTicks > TICKS_LAG_TO_REQUIRE_BIGGER_MOUSE_MOVE)
	{
		// prevent miniscrolls while finger is paused
		mouseMovedEnough = 	dv < -2 || dv > 2;
	}
	else
		mouseMovedEnough = dv != 0;
		
	if (mouseMovedEnough
		&& Button())
	{
		short newPartCode = 0;

		if (gLastScrollTicks && p.v < gLastMouse.v - 30)  
			// page down, but never on first scroll
//			newPartCode = inPageDown;
			newPartCode = inDownButton;
		else if (p.v < gLastMouse.v)  
			// scroll down
			newPartCode = inDownButton;
		else if (gLastScrollTicks && p.v > gLastMouse.v + 30)  
			// page up, but never on first scroll
//			newPartCode = inPageUp;
			newPartCode = inUpButton;
		else if (p.v > gLastMouse.v)  
			// scroll up
			newPartCode = inUpButton;
		else
			Fail("Shouldn't have gotten here in TouchScroll, strange");
		
		if (newPartCode)				
		{			
			if (gPartCode != newPartCode)
			{
				if (     gPartCode == inUpButton   && newPartCode == inDownButton
					  || gPartCode == inDownButton && newPartCode == inUpButton
					  || gPartCode == inPageUp     && newPartCode == inPageDown
					  || gPartCode == inPageDown   && newPartCode == inPageUp)
					// just flip sign on direction change
					gScrollingDV = -gScrollingDV;
				else
					gGotScrollingRectAndDV = false;
								
				gPartCode = newPartCode;
				PostClickFromPartCode(newPartCode);  // so TrackControl gets called anew with new part code
				result = kScrollNewPartCode;
			}
			else
				result = kScrollSamePartCode;
		}
	}
	
	gLastMouse = p;
	return result;
}

void PostClickFromPartCode(short partCode)
// Not callable at interrupt time because my GetOrigin() isn't and I deref unlocked handle
{
	EvQElPtr qElem;
	Point clickPt;
	const WindowPtr frontW = (**gTheScrollBar).contrlOwner;

	// mini hack:
	//
	// need to try a real-ish point here because some apps (ResEdit) stubbornly 
	// grab a copy of event->where and _PtInRect it to ensure it's in a reasonable
	// place - why are you trying to ruin my hacky patches, ResEdit?
	// 
	// *** 
	// due to uncertainty about control coordinates (see notes above) at times other
	// than when a Control Mgr call is being made, we do this by checking if the
	// contrlRect is reasonably within portRect.  if it is -- use it.  if not, the 
	// coord systems are presumably wonky, and just use a strip at the right edge of 
	// portRect.  ths point is just to get a point that the app thinks is reasonably
	// inside a control, so that, if it checks the point first, it will still later 
	// call TrackControl etc.
	
	Rect cr = (**gTheScrollBar).contrlRect;
	Rect r = (frontW)->portRect;
	
	if (cr.left >= r.left - 1 && cr.top >= r.top - 1 && cr.right <= r.right + 1 && cr.bottom <= r.bottom + 1)
		r = cr;
	else
	{
		r.left = r.right - 16;
		r.bottom -= 16;
	}
	
	LocalToGlobalRectForPort(frontW, &r);

	// post a click = mouseDown + mouseUp.  for purity could in theory post the
	// mouseUp first if a mouseUp isn't already pending but ... doesn't seem to matter
	// in practice?
	
	(void) PPostEvent(mouseDown, EVT_MSG_SIGNATURE, &qElem);
	clickPt = GetClickPtFromPartCode(&r, partCode);
	qElem->evtQWhere = clickPt;
	
	(void) PPostEvent(mouseUp, EVT_MSG_SIGNATURE, &qElem);
	qElem->evtQWhere = clickPt;
}

pascal short PatchedFindWindow(Point p, WindowPtr *w)
// so floating palettes don't "get in the way" and get returned by FindWindow
// instead of our desired scrolling window
{
	WindowPtr frontW;
	
	SetUpA4();

	if (gWeAreScrolling
		&& (WindowPtr) FrontNonPaletteWindow() == (frontW = (**gTheScrollBar).contrlOwner))
	{
		*w = frontW;  // we are scrolling, return "our" scrolly window not a palette floating atop it
		RestoreA4();
		return inContent;
	}
	else
		RESTORE_A4_AND_JUMP_TO_TOOLTRAP(FindWindow);
}

Point GetClickPtFromPartCode(const Rect * const scrollBarRect, const short partCode)
{
	Point p;
	
	switch (partCode)
	{
		case inPageDown:
			p.h = (scrollBarRect->left + scrollBarRect->right) >> 1;
			p.v = scrollBarRect->bottom - 18;
			break;
		
		case inDownButton:
			p.h = (scrollBarRect->left + scrollBarRect->right) >> 1;
			p.v = scrollBarRect->bottom - 8;
			break;
		
		case inPageUp:
			p.h = (scrollBarRect->left + scrollBarRect->right) >> 1;
			p.v = scrollBarRect->top + 18;
			break;
		
		case inUpButton:
			p.h = (scrollBarRect->left + scrollBarRect->right) >> 1;
			p.v = scrollBarRect->top + 8;
			break;
		
		default:
			Fail("TouchScroll error - Bad part code in GetClickPtFromPartCode()");
			break;
	}
	
	return p;
}

pascal short PatchedFindControl(Point p, WindowPtr w, ControlHandle *c)
{
	short result;
	Boolean didIt = false;
	
	SetUpA4();
	
	// TODO add a check to ensure this is really from our click? hasn't seemed needful though

	if (gWeAreScrolling
		&& gPartCode 
		&& w == (**gTheScrollBar).contrlOwner)
	{
		Rect scrollBarRect = (**gTheScrollBar).contrlRect;
		gClickPt = GetClickPtFromPartCode(&scrollBarRect, gPartCode); 

		result = CALL_ORIG_TRAP(FindControl) (gClickPt, w, c);
		
		if (*c == gTheScrollBar)
		{
			if (result == gPartCode)
				didIt = true;
			else
			{
				// our click didn't hit the part we expected. two explanations:
				// 1. this is just a weird control or we did something wrong -- bail out
				//	  (leave didIt == false)
				// 2. we tried to click in page up/down region but the thumb is there (at
				//    top/bottom of scroll bar) -- turn into an up/down arrow click instead:
			
				if (result == inThumb 
					&& (gPartCode == inPageUp || gPartCode == inPageDown))
				{
					gPartCode -= 2;  // convert pageUp/Down to up/down arrow
					gClickPt = GetClickPtFromPartCode(&scrollBarRect, gPartCode);
					result = CALL_ORIG_TRAP(FindControl) (gClickPt, w, c);
					
					if (result == gPartCode && *c == gTheScrollBar)
						didIt = true;
				}
			}
		}								
	}
	
	if (!didIt)
		result = CALL_ORIG_TRAP(FindControl) (p, w, c);

	// note, this one is a tail patch
	
	trace("set gNSBLastFindControlPartCode to %d", result);
	gNSBLastFindControlPartCode = result;				

	RestoreA4();

	return result;
}

void CallActionProc(ControlHandle c, ActionProc action, short partCode)
// given a control, partcode, and an action proc, call the action proc 
// (or the control's default action proc if 'action' is odd)
{
	// note - DebugStr is slow!  putting any in here (since it's called in a loop)
	// will visibly slow inertial scrolling, even with 'dx' off in Macsbug
	
	register long *savedA7, *savedA5;

	asm { 
		movea.l a7, savedA7
		movea.l a5, savedA5
	}

	gWeAreInActionProc = true;
	
	if (gFrontProcessCreator == 'KAHL')  // we're running THINK Project Manager
	{
		// A little hack that I couldn't do any other way:
		// before it calls TrackControl, 
		//
		// * the 'THINK C' THINK Project Manager 6.0.1 saves TICKS+10 at -0x54B2(A5).  
		// * the 'Symantec C++ 6.0' THINK Project Manager does it at -0x5314(A5).  
		// * the 'Symantec C++ 7.0' THINK Project Manager does it at -0x59F0(A5).
		//
		// Any calls to the actionProc it provides to
		// TrackControl (after the first one) are ignored until that time, to give
		// a slow start to scrolling.  It doesn't call TickCount to check the time,
		// so to prevent this delay, I check those memory locations explicity.  If
		// either are greater than TICKS but <= TICKS+10, I assume it's THINK's scroll
		// delay time, and I replace it with the current value of TICKS.  This makes
		// scrolling much smoother-looking in THINK C files, which was an important
		// use case since I literally was developing this in THINK C on an iPad
		// and found TouchScroll very useful in the editor during its own construction...
		//
		// TODO check/make this work for other versions of THINK C/SymC++!

		if (gThinkCVersion == 0x0600)  // maybe it's Symantec C++ 6.0
		{
			long * const thinkProjMgrTicks = (long *) (((long) savedA5) - 0x5314);

			if (*thinkProjMgrTicks > TICKS && *thinkProjMgrTicks <= TICKS + 10)
				*thinkProjMgrTicks = TICKS;
		}		
		else if (gThinkCVersion >= 0x0700)  // maybe it's Symantec C++ 7.0
		{
			long * const thinkProjMgrTicks = (long *) (((long) savedA5) - 0x59F0);

			if  (*thinkProjMgrTicks > TICKS && *thinkProjMgrTicks <= TICKS + 10)
				*thinkProjMgrTicks = TICKS;
		}
		else if (gThinkCVersion >= 0x0601)  // act like it's THINK C 6.0.1
		{
			long * const thinkProjMgrTicks = (long *) (((long) savedA5) - 0x54B2);

			if (*thinkProjMgrTicks > TICKS && *thinkProjMgrTicks <= TICKS + 10)
				*thinkProjMgrTicks = TICKS;
		}		
	}

	if (action)
	{
		if (!((long) action & 1L))
			// 'action' is even (and non-null):  just call it
			action(c, partCode);
		else
		{
			// 'action' is odd:  get control's default action proc, if any
			const ActionProc contrlAction = (ActionProc) (**c).contrlAction;

			if (contrlAction)
			{
				if ((long) contrlAction & 1L)
					Fail("odd action proc whaaaaatf");
				else
					contrlAction(c, partCode);
			}
		}
	}
		
	asm {
		cmp.l a7, savedA7
		beq @stackStillOKAfterAfterProc
	}

	// if we're here, stack got unbalanced; did we call wrong type of action proc ????
	Fail("stack messed up after action proc, o noes");
	SysError(1002);

stackStillOKAfterAfterProc:
	gWeAreInActionProc = false;
}

void SetInertialVelocityAndDecel(short inertialInterval, 
	Fixed *inertialVelocity, 
	Fixed *inertialDecel)
{
	*inertialVelocity = FixRatio(1, inertialInterval);
	*inertialDecel = *inertialVelocity / INERTIAL_DECEL_RATIO;	

	if (*inertialDecel < 1)
		*inertialDecel = 1;
}

short CalcInertialInterval(long dt, short dv)
{
	const int pixelsPerRow = 
		gGotScrollingRectAndDV && gScrollingDV 
			? ABS(gScrollingDV) 
			: kDefaultPixelsPerRow;

	const long ii = dt * MILLISECONDS_PER_TICK
					/ (ABS(dv) / pixelsPerRow + 1)
					+ 1;  // don't let it round to zero

	if (ii <= MAX_INERTIAL_MILLISECONDS)
		return (short) ii;
	else
		return MAX_INERTIAL_MILLISECONDS + 1;  // prevent short overflow
}

Boolean StartPartialScrollTimerIfApplicable(Timer *partialScrollTimer, 
	short inertialInterval, long lastPartialScrollDurationMS)
// "partial scrolling" == purely cosmetic scrolling a few pixels at a time (less than a full 
// text line/scrollbar increment) to smooth things out visually
{
	if (inertialInterval > MIN_INERTIAL_INTERVAL_TO_PARTIALLY_SCROLL)
	{
		// don't partially scroll if last time we tried, it took
		// more than the current desired partialScrollInterval			
		const int kMinPartScrollMS = 
				MAX(lastPartialScrollDurationMS, MIN_PARTIAL_SCROLL_MILLIS);
				
		const int kMaxPartScrollMS = inertialInterval >> 1;
		
		if (kMinPartScrollMS <= kMaxPartScrollMS)
		{
			StartTimer(partialScrollTimer, kMinPartScrollMS);
			return true;
		}
	}
	
	return false;
}

void DoInertialMode(ControlHandle c, ActionProc action, short inertialInterval)
// "intertial mode" == user has lifted finger off touchpad, but we keep scrolling for a while
// assumes port already set up for us (by PatchedTrackControl)
{
	Timer realScrollTimer, partialScrollTimer;
	Boolean readyToPartiallyScroll = false;
	Boolean doneScrolling = false, userStopped = false;
	long lastPartialScrollDurationMS = 0;
	short startDragMouseY;
	Boolean wasDown = false;	
	long mouseDownTime;
	long lastDragCheckTime;
	Boolean quickClick = false;
	Boolean slowOrBackwardsDrag = false;
	Boolean mouseDownAndStationary = false;
	Boolean turboMode = false;
	Boolean turboKeyWasDown = false;
	
	Boolean needToCheckScrollSpeed = true;
	short numTimesScrolledSlowerThanDesired = 0;
	short numTimesSlowOrBackDrag = 0;

	// inertialInterval is in milliseconds
	// inertialVelocity, viewed as a Fixed, is in units of 'scrolls per ms'
	//               ... viewed as a long, is in units of 'scrolls per 65536 ms'
	Fixed inertialVelocity;
	Fixed inertialDecel;

	unsigned long lastScrollMics, lastPartialScrollMics;

	if (!action)
		return;

	trace("entering Doinertialmode");

	SetUpTimer(&realScrollTimer);
	SetUpTimer(&partialScrollTimer);
	
	if (gPartCode == inPageUp || gPartCode == inPageDown) 
	{
		gPartCode -= 2;     // convert pageUp/Down to up/down arrow
		inertialInterval = BIG_SWIPE_STARTING_INERTIAL_INTERVAL;  // start inertial mode at fastest possible rate
	}

	// we now have final inertialInterval; set intertialVelocity used to determine
	// subsequent deceleration
	
	SetInertialVelocityAndDecel(inertialInterval, &inertialVelocity, &inertialDecel);

	// init partial scrolling

	gPartiallyScrolled = 0;

	ObscureCursor();  // hide cursor while in inertial mode until moved again

	// start timers (first partial, then real):

	lastScrollMics = lastPartialScrollMics = Microseconds();

	if (inertialInterval > MIN_INERTIAL_INTERVAL_TO_PARTIALLY_SCROLL
		&& gGotScrollingRectAndDV)  // can't partially scroll without gScrollingRectAndDV
	{
		// first partial scroll is always just half the inertialInterval, below
		// we'll try smaller intervals maybe
		StartTimer(&partialScrollTimer, inertialInterval >> 1);
		readyToPartiallyScroll = true;
	}

	StartTimer(&realScrollTimer, inertialInterval);
	
	while (!doneScrolling)
	{
		// need to check for a few mouse actions here:  if user presses the mouse button...
		// 1. and drags slowly, is still, or goes backwards -- stop inertial mode
		// 2. drags fast the right way, speed it up
		// 3. releases quickly, that's just a click -- stop inertial mode
		
		const Boolean btn = Button();	
		turboKeyWasDown |= KEY_DOWN(TURBO_KEY);
		
		if (btn && !wasDown)
		{
			// mouse is newly down
			startDragMouseY = MOUSE.v;
			mouseDownTime = lastDragCheckTime = TICKS;
			wasDown = true;
		}
		else if (!btn && wasDown)
		{
			// mouse was down but now released
			
			if (TICKS - mouseDownTime <= 3)
				quickClick = true;  // it was a quick click, stop scrolling
			
			wasDown = false;
			ObscureCursor();  // re-hide cursor until moved again
		}
		else if (btn && wasDown && TICKS >= lastDragCheckTime + 2)
		{
			// mouse was already down -- it's been 2 ticks since last checkin,
			// see what's happening (can't use 1 tick above because might straddle a tick
			// boundary with much shorter elapsed time)

			const short dv = MOUSE.v - startDragMouseY;
			const short kSlowDragThreshold = 5;
			
			if  (   gPartCode == inDownButton && dv > -kSlowDragThreshold
			     || gPartCode == inUpButton   && dv < +kSlowDragThreshold)
			{
				if (++numTimesSlowOrBackDrag >= 2)
				{
					slowOrBackwardsDrag = true;
					// (we will now exit DoInertialMode, and since we only checked the
					// mouse button state with Button(), the still-pending mouseDown will
					// get caught by our SystemEvent patch and re-initiate a drag-scroll
					// in the other direction if needed)
				}
				
			}
			else  // a fast-enough drag, maybe speed up inertialInterval
			{
				short newInertialInterval = 
					CalcInertialInterval(TICKS - lastDragCheckTime, dv);

				numTimesSlowOrBackDrag = 0;
				
				if (   newInertialInterval > inertialInterval - inertialInterval/5
					&& newInertialInterval <= inertialInterval + inertialInterval/5)
				{
					// new interval approximately the same as old one --
					// assume user was trying to speed it up
					newInertialInterval = inertialInterval - inertialInterval/5;
				}
	
				if (newInertialInterval < inertialInterval)
				{
					inertialInterval = newInertialInterval;
					
					SetInertialVelocityAndDecel(inertialInterval, &inertialVelocity, &inertialDecel);
					
					needToCheckScrollSpeed = true;
					numTimesScrolledSlowerThanDesired = 0;
				}

				mouseDownTime = 0;  // user tried a fast drag; don't treat any mouseUp as a "quick click"
			}
			
			lastDragCheckTime = TICKS;
		}	

		// are we done scrolling?
		
		userStopped =
				   quickClick
				|| slowOrBackwardsDrag
				|| ANY_KEY_DOWN_EXCEPT_TURBO();

		doneScrolling = 
         		   inertialInterval >= MAX_INERTIAL_MILLISECONDS
         		|| inertialVelocity <= 0
				|| userStopped;

		// did the user maybe just start a click and hasn't moved mouse yet -- possibly
		// trying to stop scrolling -- but not held long enough to call it a 
		// "slowOrBackwardsDrag" yet?  don't scroll under a still mouse, looks weird
		mouseDownAndStationary = btn && MOUSE.v == startDragMouseY;

		// do we need to partially scroll?
		
		if (!doneScrolling
			&& !mouseDownAndStationary
			&& readyToPartiallyScroll
			&& !RUNNING(partialScrollTimer)  // partial scroll timer expired
			&& RUNNING(realScrollTimer)) 	 // but full-scroll timer hasn't yet
		{
			// yes -- time to partially scroll

			long partialDTMillis = 0;
			const unsigned long partialMicros = Microseconds();

			// can't check partiaDTMillis < 0 later (unsigned subtraction) so do this here:			
			partialDTMillis = (partialMicros >= lastPartialScrollMics) 
				? (partialMicros - lastPartialScrollMics >> 10) : 0;

			readyToPartiallyScroll = false;

			if (gGotScrollingRectAndDV)  // in case app didn't call ScrollRect or CopyBits somehow
			{
				Boolean gottaStopAlmostAtEnd = false;
				
				// compute how much to partially scroll based on elapsed time
				short partialDV = partialDTMillis * gScrollingDV / inertialInterval;
	
				if (gScrollingDV > 0)  // cap this in case a bunch of time went by!
					partialDV = MIN(partialDV, gScrollingDV - gPartiallyScrolled);
				else
					partialDV = MAX(partialDV, gScrollingDV - gPartiallyScrolled);

				// small optimization -- don't "partially scroll" the entire 
				// remaining bit to next full scroll increment, since
				// we will have to do full scroll anyway to update
				if (partialDV == gScrollingDV - gPartiallyScrolled)
					partialDV = 0;
				
				if (partialDV)  // (also ensures didn't round to 0)
				{
					// prevent overshoot (happens often e.g. in Finder)
					//
					// note -- this assumes scroll bar units are pixels;
					// could check this by comparing control value before vs after 
					// I call action proc -- but will probably just leave it this way

					const int newVal = (**c).contrlValue - 
						(gPartiallyScrolled + partialDV);
					
					gottaStopAlmostAtEnd = 
						newVal <= (**c).contrlMin || newVal >= (**c).contrlMax;

					if (!gottaStopAlmostAtEnd)
					{
						// partially scroll!
											
						const long partialScrollStartTicks = TICKS;
						
						gDoSimpleScroll = true;
						{
							Rect localRect = gScrollingRect;
							GlobalToLocalRectForPort(THEPORT_FROM_CURRENTA5, &localRect);
							CALL_ORIG_TRAP(ScrollRect) (&localRect, 0, partialDV, gJunkRgn);
						}
						gDoSimpleScroll = false;
	
						lastPartialScrollDurationMS = (TICKS - partialScrollStartTicks) * MILLISECONDS_PER_TICK;
						lastPartialScrollMics = partialMicros;
						
						gPartiallyScrolled += partialDV;

						trace("--- partially scrolled %d (total %d) after %l ms", partialDV, gPartiallyScrolled, partialDTMillis);
					}
				}

				// restart timer to partially scroll again if appropriate:
				
				if (!gottaStopAlmostAtEnd
					&& ABS(gScrollingDV) > ABS(gPartiallyScrolled))  // anything left?
				{
					readyToPartiallyScroll = StartPartialScrollTimerIfApplicable(
							&partialScrollTimer, inertialInterval, lastPartialScrollDurationMS);
				}
			}
		}
		
		// do we need to scroll for real?  do this for two reasons:
		//
		//    1. realScrollTimer expired
		//
		//    2. user did click/keypress so need to stop inertial scrolling,
		//       AND we had already partially scrolled, so need to "complete" that partial scroll
		//       to avoid leaving visual artifact

		if (  !doneScrolling 
				&& !mouseDownAndStationary 
				&& !RUNNING(realScrollTimer)  	// time to scroll for real?
			|| doneScrolling 
				&& gPartiallyScrolled			// or, done scrolling but need true-up?
			|| doneScrolling
				&& !userStopped					// or, we slowed down enough that
				&& turboKeyWasDown 				// should stop, but turbo key is down
				&& !turboMode)					// and should instead enter turbo mode?
		{
			const int oldVal = (**c).contrlValue;
			const long giveUpTicks = TICKS + 30;
			long dtMillis = 0;

			// restart timers (BEFORE calling actionProc -- to keep timing smooth)
			
			// to get visuals right, we decelerate linearly in "velocity" space
			// (scrolls per second), not in "delay" space (inertialInterval)

			const unsigned long micros = Microseconds();

			// can't check dtMillis < 0 later (unsigned subtraction) so do this here:			
			dtMillis = (micros >= lastScrollMics) ? (micros - lastScrollMics >> 10) : 0;
			lastScrollMics = lastPartialScrollMics = micros;

			if (dtMillis < 1)  // Mini vMac issue can sometimes make Microseconds go backwards!
				dtMillis = 1;
			
			// if just started scrolling (or sped up from a mouse drag or entering
			// turbo mode), check if underperforming target speed
			// 3 times in a row -- if so, reduce target speed
			//
			// (we do this because otherwise, if we set velocity much faster than we
			// can actually scroll, we can decel a bunch of times and not visibly
			// impact speed.  then suddenly we visibly slow very abruptly once we 
			// cross threshold of velocity being slower than real scrolling speed.)

			if (needToCheckScrollSpeed)
			{
				if (dtMillis > inertialInterval)
				{
					trace("too fast");
					if (++numTimesScrolledSlowerThanDesired >= 3)
					{
						// slow it down
						inertialInterval = dtMillis;
						SetInertialVelocityAndDecel(inertialInterval, &inertialVelocity, &inertialDecel);
						needToCheckScrollSpeed = false;
						trace("too fast 3 times - slowing down, new ii = %d", inertialInterval);
					}
				}
				else
					needToCheckScrollSpeed = false;
			}

			// decelerate as needed

			if (!turboKeyWasDown)
			{
				// (it's OK for this to drop negative, will catch negative 
				// inertialInterval at top of loop)
				inertialVelocity -= inertialDecel * dtMillis;

				if (inertialVelocity > 0)
					inertialInterval = (short) FixDiv(1, inertialVelocity);
			}
			
			// check if need to enter/exit turbo mode

			if (!userStopped)
			{
				turboKeyWasDown |= KEY_DOWN(TURBO_KEY);  // check again

				if (turboKeyWasDown && !turboMode)
				{
					// enter turbo mode
					inertialInterval >>= 3;

					if (inertialInterval > (MAX_INERTIAL_MILLISECONDS >> 3))
						inertialInterval = (MAX_INERTIAL_MILLISECONDS >> 3);
						
					if (!inertialInterval) inertialInterval = 1;  // don't round to 0

					SetInertialVelocityAndDecel(inertialInterval, &inertialVelocity, &inertialDecel);

					needToCheckScrollSpeed = true;
					numTimesScrolledSlowerThanDesired = 0;
					turboMode = true;
					doneScrolling = false;  // try to keep going

				}
				else if (!turboKeyWasDown && turboMode)
				{
					// exit turbo mode
					turboMode = false;
				}
			}
	
			// restart partial scroll timer if applicable
			
			StopTimer(&partialScrollTimer);
			
			readyToPartiallyScroll = StartPartialScrollTimerIfApplicable(
				&partialScrollTimer, inertialInterval, lastPartialScrollDurationMS);

			// start real scroll timer -- we do after any partial timer just to
			// ensure no weird slowdown here makes this one go off first!
			
			StartTimer(&realScrollTimer, inertialInterval);
								
			// call action proc:  apps will sometimes impose
			// a scroll delay and ignore our actionProc call if too soon!
			// (we have a special case for this for THINK C only, search above for 'KAHL')
			// if this happened, keep looping thru to call actionProc ASAP ...			
			
			trace("--- scrolling for real");
			do
				CallActionProc(c, action, gPartCode);
			while ((**c).contrlValue == oldVal && TICKS < giveUpTicks);			

			if (   gPartCode == inUpButton   && (**c).contrlValue <= (**c).contrlMin
				|| gPartCode == inDownButton && (**c).contrlValue >= (**c).contrlMax)
			{
				// scrolled to end -- bounce, then stop
				
				if (needToCheckScrollSpeed && numTimesScrolledSlowerThanDesired > 0)
				{
					// we noticed we aren't scrolling as fast as desired but didn't
					// get to 3 times yet ... go ahead and adjust now so we don't
					// give DoRubberBand() a too-fast speed and (since it runs a tighter
					// loop) suddenly observe a jerky speedup	
					inertialInterval = dtMillis;
					SetInertialVelocityAndDecel(inertialInterval, &inertialVelocity, &inertialDecel);
				}

				// rubberband when inertial scroll hits the end
				DoRubberBand(c, inertialVelocity * ABS(gScrollingDV), lastScrollMics);

				// - note - previously was as below, but that double-decremented given above code:
				// DoRubberBand(c, (inertialVelocity - inertialDecel * dtMillis) 
				// 				  * ABS(gScrollingDV),
				// 			 lastScrollMics);

				doneScrolling = true;
			}
		}
			
		turboKeyWasDown = false;  // we processed any prior cmd key press
	}

	RestoreCursor();

	CleanUpTimer(&realScrollTimer);
	CleanUpTimer(&partialScrollTimer);

	gPartiallyScrolled = 0;  // so subsequent ScrollRect/CopyBits patches don't get confused
}

void RestoreScrolledBackPixels(Offscreen *offscreen, Rect *offRect, int scrolledDist, int scrolledAmt)
// called from DoRubberBand:
//
// 	 scrolledDist:  total distance scrolled BEFORE this scroll (absolute value)
//	 scrolledAmt:   amount just passed to ScrollRect (signed)
{
	Rect restoreRect = *offRect;
	
	if (gScrollingDV > 0)  // restore pixels at bottom
	{
		restoreRect.top    = offRect->bottom - scrolledDist;
		restoreRect.bottom = restoreRect.top - scrolledAmt /*<0*/;

		if (restoreRect.bottom >= SCREENBITS_FROM_CURRENTA5.bounds.bottom - 5
			&& (restoreRect.left <= 5 || restoreRect.right >= SCREENBITS_FROM_CURRENTA5.bounds.right - 5))
		{
			// special case:  if the rect we are restoring includes either bottom corner
			// of the screen, then the OLD bottom corner of the screen was just scrolled
			// up.  that includes up to 5 rows of missing round-rect corner pixels,
			// so restore up to an extra 5 rows above our specified rect:
			
			restoreRect.top = MAX(restoreRect.top - 5, offRect->top);
			trace("adjusting restoreRect.top due to bottom edge");
		}
		
		RestorePixelsWithOffset(offscreen, &restoreRect, 0, offRect->bottom - restoreRect.bottom);
	}
	else  // restore pixels at top
	{
		restoreRect.bottom = offRect->top + scrolledDist;
		restoreRect.top    = restoreRect.bottom - scrolledAmt /*>0*/;
		RestorePixelsWithOffset(offscreen, &restoreRect, 0, offRect->top - restoreRect.top);
	}
}

Boolean GrabScrollingRectAndDV(void)
// if gGotScrollingRectAndDV, return true
// else fake it using the NSB scrolling rect if possible
// else return false
{
	if (gGotScrollingRectAndDV)
		return true;
	else
	{
		// if the only scrolls were page up/down, or in some cases if an app scrolls
		// using CopyBits from a buffer (instead of using ScrollRect, or using CopyBits
		// like ScrollRect to move pixels on the same bitmap) we know the rect but 
		// don't have gScrollingDV -- that's OK, just guess one based on direction:
		
		if (gNSBGotScrollingRect && gNSBLastFindControlPartCode)
		{
			gScrollingRect = gNSBScrollingRect;
			
			if (gNSBLastFindControlPartCode == inUpButton || gNSBLastFindControlPartCode == inPageUp)
				gScrollingDV = kDefaultPixelsPerRow;
			else
				gScrollingDV = -kDefaultPixelsPerRow;
			
			return true;
		}
	}

	return false;
}

void UnhiliteControl(ControlHandle c)
// turn off any hiliting, and redraw scroll bar				
{
	const Byte hilite = (**c).contrlHilite;
	(**c).contrlHilite = 0;
	Draw1Control(c);
	(**c).contrlHilite = hilite;  // put it back just in case
}

void DoRubberBand(ControlHandle c, Fixed vel, unsigned long lastScrollMics)
// "rubber banding" is when you keep scrolling past the end, and the content goes a little
// "too" far with decreasing velocity as you keep dragging to some limit -- then bounces back
// into place when you release
//
// if specified, 'vel' is in pixels (not 'scrolls') per millisecond
//
// two ways to enter:
//   1. with some inertial velocity 'vel' the wrong way (always >= 0 --> wrong way),
//      which could be due to an "NSB" bouce
//   2. based on mouse dragging past the end (vel = 0)
//
// either way, if mouse is down, user can drag either way -- on release we always
// move in 'right' direction with inital vel based on how far offsides we are.
// user can always mouse down again!
{
	// if we were called without gWeAreScrolling, must be an NSB bounce:
	const Boolean nsbBounce = !gWeAreScrolling;
	
	Offscreen offscreen;
	int startMouseY;
	int scrolledDist = 0;  		// absolute value
	int maxScrolledDist = 0;	// most distance ever scrolled
	Fixed decel = 0;
	Rect offRect, visRect;
	Boolean savedPixels = false;
	Boolean firstTimeThru = true;
	Boolean mouseWasDown;
	Boolean needFullUpdate = false;
	GrafPtr savePort;
	unsigned long oldLastScrollMics;
	int scrollDistLimit;
	const WindowPtr frontW = (WindowPtr) FrontNonPaletteWindow();

	// validate we are set up OK

	if (vel < 0)  // could happen because we are passed inertialVelocity minus some decel
		vel = 0;

	trace("in DoRubberBand");

	if (!GrabScrollingRectAndDV())
	{
		// this can happen in apps that don't use either ScrollRect or CopyBits
		trace("exiting DoRubberBand bc can't GrabScrolling...");
		goto doneRubberBand;
	}
	
	// don't rubberband more than half window height -- mainly to limit
	// offscreen pixmap size (640x480 in 8 bit color = 300K!)

	scrollDistLimit = (gScrollingRect.bottom - gScrollingRect.top) >> 1;

	// set up offscreen pixmap with height = maxScrollDist, dealing with case
	// where window is partially offscreen, so scrolling pixels to the screen
	// edge (not necesarilly all the way off the window) requires saving pixels
	// "less distant" than expected --
	//
	// more generally, can think of this as fully dealing with the case where the full 
	// full scrolling rect is not in the visRgn, but the intersection is still
	// *rectangular*:
	
	offRect = gScrollingRect;  // in global coords
	visRect = (**frontW->visRgn).rgnBBox;
	LocalToGlobalRectForPort(frontW, &visRect);
	(void) SectRect(&offRect, &visRect, &offRect);  // part of offRect may have been offscreen

	// we do two things here:
	//
	// 1.  truncate offRect to only include the portion of the visRect in direction of
	//     scrolling 
	//
	// 2.  check to see if we will need a full update via InvalRect() when done rubber-
	//     banding, which is necessary if the visRgn is nonrectangular (because portions
	//     of the window's bitmap that move under an obtruding floating palette etc.
	//     can't be redrawn by scrollrect when they come back).  However if scrolling
	//     up, we first confirm that any nonrectangularity isn't just due to the rounded
	//     corners at the bottom of the screen (by truncating up to the bottom 5 pixels
	//     of the visRgn before checking if it's nonrectangular).  got that?

	{
		RgnHandle visRgnToCheck;
			
		if (gScrollingDV > 0)
		{
			// scrolling down -- use bottom of offRect
			offRect.top = MAX(offRect.top, offRect.bottom - scrollDistLimit);
			visRgnToCheck = frontW->visRgn;
		}
		else
		{
			Point scrn_botright = botRight(SCREENBITS_FROM_CURRENTA5.bounds);
			
			// scrolling up -- use top of offRect
			offRect.bottom = MIN(offRect.bottom, offRect.top + scrollDistLimit);

			// old note here:
			//	TODO should be checking OFFRECT here not visrect ....
			//	or rather, the intersection of visRgn with gScrollingRect?
			// but isn't that what I AM doing?
			trace("about to check corners");

			if (visRect.bottom >= scrn_botright.v - 5
				&& visRect.top < scrn_botright.v - 5  // avoid degenerate case
				&& (visRect.left <= 5 || visRect.right >= scrn_botright.h - 5))
			{
				// bottom of visRect is within bottom 5 pixels of screen, so we could
				// be obscured by rounded screen corners -- truncate bottom of visRgn
				// by 5 pixels before checking (below) if it's nonrectangular, since
				// we don't need to treat rounded-screen-corner case as a non-
				// rectangular visRgn requiring a full update (only need to deal with
				// partially obtruding floating palettes etc in that they may take
				// a "notch" out of the side of our window partway up, which rounded
				// screen corners don't do since they're ... at the corners)
				
				const RgnHandle adjVisRectRgn = NewRgn();
				Rect localVisRect;

				visRgnToCheck = NewRgn();
				CopyRgn(frontW->visRgn, visRgnToCheck);

				visRect.bottom = scrn_botright.v - 5;
				localVisRect = visRect;
				GlobalToLocalRectForPort(frontW, &localVisRect);

				RectRgn(adjVisRectRgn, &localVisRect);
				SectRgn(visRgnToCheck, adjVisRectRgn, visRgnToCheck);
				
				DisposeRgn(adjVisRectRgn);

				trace("chopped off bottom slice, visRect is %r", visRect);
			}
			else
				visRgnToCheck = frontW->visRgn;
		}
		
		// we now have the right possibly-truncated visRgn in visRgnToCheck --
		// is it nonrectangular?
		if (!IS_RECT_RGN(visRgnToCheck))
		{
			needFullUpdate = true;  // yes -- need to call InvalRect() below
			trace("visrgn nonrectangular");
		}
		
		if (visRgnToCheck != frontW->visRgn)  // make sure we called NewRgn()!
			DisposeRgn(visRgnToCheck);
	}
		
	if (offRect.top == offRect.bottom)
		goto doneRubberBand;
	
	// set port, just in case
	
	GetPort(&savePort);
	SetPort((GrafPtr) frontW);

	if (SetUpOffscreen(&offscreen, &offRect, 0, TRY_TEMP_MEM))
	{
		if (!LockPixels(GetGWorldPixMap(offscreen.g)))
			CleanUpOffscreen(&offscreen);  // oops, never mind
		else
		{
			// we are good to go offscreen
			SavePixels(&offscreen, &offRect);
			savedPixels = true;

			{
				// when we scroll underneath floating palettes, a visual artifact is introduced
				// when we "rubberband" and temporarily redraw the floating palette contents in the
				// wrong place (to see, put macpaint pattern palette near bottom of screen, e.g.)
				//    
				// fix is to erase the DiffRgn = (offscreen bitmap rect - 
				// front window's visrgn) from the offscreen bitmap after savings pixels 
				// offscreen, so we do that here
				//
				// more generally, this deals with the case where the full scrolling rect is
				// not in the visRgn, and the intersection is *not rectangular*:
		
				GDHandle saveDevice;
				CGrafPtr savePort;
				Rect localOffRect = offRect;
				const RgnHandle tempRgn = NewRgn();
				
				GlobalToLocalRectForPort((GrafPtr) frontW, &localOffRect);
				RectRgn(tempRgn, &localOffRect);
				DiffRgn(tempRgn, frontW->visRgn, tempRgn);

				// tempRgn is now the region from the offRect that is NOT in the
				// window's visRgn -- erase any pixels blitted from there (e.g., part
				// of a floating pallete obtruding) in the offscren bitmap:
				
				if (!EmptyRgn(tempRgn))
				{
					Point offset = GET_GLOBAL_COORD_OFFSET(frontW);
					OffsetRgn(tempRgn, -offset.h, -offset.v);  // now in global coords

					GetGWorld(&savePort, &saveDevice);
					SetGWorld(offscreen.g, NULL);
					EraseRgn(tempRgn);
					SetGWorld(savePort, saveDevice);
					
					// when scrolling back from rubberband, we could be revealing pixels
					// (with ScrollRect) that were "behind" a floating palette -- these
					// won't get drawn, so remember to InvalRect when we're done (the above
					// EraseRgn prevents us from instead drawing nonsense -- but we still
					// do need to have the app draw the RIGHT thing):
					needFullUpdate = true;
				}
				
				DisposeRgn(tempRgn);	
			}
		}
	}

	// initialize scrolling things
	
	if (vel)
	{
		// time to decel to zero, in ms, as a Fixed
		// note the integer constant must be a long here, or it gets left-shifted into oblivion
		const Fixed kDecelTime = 200L << 16;
		
		decel = FixDiv(vel, kDecelTime);
		trace("decel %l vel %l kDecelTime %l", decel, vel, kDecelTime);

		if (decel < 1)
			decel = 1;

		// we got here due to inertial velocity
		mouseWasDown = false;	
	}
	else
	{
		trace("no vel, we are being dragged, gScrollingDV is %d at address %xl", gScrollingDV, &gScrollingDV);
		// we got here because user dragged past end
		mouseWasDown = true;
		startMouseY = MOUSE.v - gScrollingDV;  // force initial wrong-way scroll on entry
	}
	
	// loop as long as we remain "wrong-way" scrolled, or we have velocity in that
	// direction (note:  vel is always nonnegative; vel > 0 means we have wrong-way
	// velocity; once it hits zero, right-way velocity [with mouse up] is always just
	// a scalar times distance)
	
	do
	{
		short scroll = 0;  // amount to scroll this time thru; >0 --> "wrong way"
		const Fixed oldVel = vel;
			
		if (!nsbBounce && Button())
		{
			// mouse is down, but not an NSB bounce:
			// user is manually scrolling in the rubberband region
			
			const short curMouseY = MOUSE.v;
			short signedMouseDV = 0;
			
			if (!mouseWasDown)
			{
				ShowCursor();
				mouseWasDown = true;
				startMouseY = curMouseY;
			}
			
			// how far has mouse moved the "wrong way"?
			signedMouseDV = gScrollingDV > 0 ? curMouseY - startMouseY 
											 : startMouseY - curMouseY;

			if (signedMouseDV > 0)
			{
				// if mouse has moved by total of y, we scroll by a total of log(m(y+1))/m.
				// this has the logarithmic shape we want while also having a derivative
				// that starts at 1 then shrinks (so, even for small y, we never scroll
				// faster than linear, which would look weird).  (this is of course a natural log)
				
				const float logScrollMult = 0.015;  // bigger number = more resistance to scrolling
				
				scroll = log(logScrollMult * signedMouseDV + 1)/logScrollMult  // this term is total desired scroll dist
						 - scrolledDist;
			}
			else
				// mouse has net moved "right way", scroll all the way back
				scroll = -scrolledDist;
		}
		else
		// mouse is up, or we don't have gWeAreScrolling e.g. because it's an NSB bounce:
		// for vel > 0 scroll the "wrong way" inertially,
		// else vel == 0 and bounce back the "right way" based on distance to return
		{
			unsigned long micros;
			long dtMillis;

			if (mouseWasDown)
				ObscureCursor();
			
			if ((mouseWasDown || firstTimeThru) && !lastScrollMics)
				// don't do this earlier to prevent oversized first scroll due to big dtMillis
				micros = lastScrollMics = Microseconds();
			else
				micros = Microseconds();

			firstTimeThru = false;			
			mouseWasDown = false;				

			{
				const unsigned long dtMicros = micros - lastScrollMics;

				if (micros < lastScrollMics)  // can't check dtMicros < 0 because unsigned
					// micros rolled over -- or weird Mini vMac edge case where
					// _Microseconds goes backwards -- throw out old values just
					// in case
					oldLastScrollMics = lastScrollMics = micros;

				//trace("micros %l, lastscrollmics %l", micros, lastScrollMics);				
				if (dtMicros >= 1024)  // use 1024 because of the >>10
					dtMillis = dtMicros >> 10;
				else  // (we know dtMicros > 0 because it's unsigned)
				{
					MiniDelay(-(1024 - dtMicros));  // wait til 1 total millisecond
					dtMillis = 1;
					//trace("dtMicros was %l before minidelay", dtMicros);
				}

				// we save this in case not enough time elapsed to scroll
				// (ABS(scroll) < 1 below), in which case we bump back lastScrollMics
				// to this prior value and wait til next time (just bumping up to 
				// scroll +/-1 can be too fast/make us scroll too far while decelerating!)
				oldLastScrollMics = lastScrollMics;				
				lastScrollMics = micros;
			}

			if (vel && nsbBounce && !Button())
			{
				// if an NSB bounce, snap back when button released
				vel = 0;
				UnhiliteControl(c);
			}
			else if (vel)
			{
				// we have inertial velocity:
				// vel is a Fixed in units of pixels per millisecond, convert here
				// to # pixels to scroll right now given dtMillis:
				Fixed avgVel;

				// vel;  velocity next time, if <=0 we're done
				// avgVel: velocity this time (>= vel), if >= 0 we scroll this time (but still
				// stop if vel <= 0)				
				vel -= decel * dtMillis;
				avgVel = (oldVel + vel) >> 1;

				if (avgVel > 0)
				{
					scroll = (avgVel * dtMillis) >> 16;
					//trace("we will scroll %l (avgVel fixed) * %l (dtMillis) = %l (fixed) = %d (scroll)", avgVel, dtMillis, (long) (avgVel * dtMillis), scroll);

					if (scroll < 1)  // not enough time elapsed to scroll a pixel
					{
						vel = oldVel;  // so once we do scroll, avg vel is right
						lastScrollMics = oldLastScrollMics;  // see note above
						trace("extending time with scroll = %d, vel now %l fixed", scroll, vel);
					}
				}
				else
					;  // enough time has gone by that we should now be reverse-scrolling, loop back thru
			}
			else
			{
				// no inertial velocity - we are snapping back -
				// scroll back the "right way" based on distance to return:
				// velocity (pixels/millisecond) drops from -kVelScale*scrolledDist 
				// down to 0 depending on scrolledDist remainig --
				// BUT don't do it if button down (e.g. did regular scroll past
				// end and are NSB bouncing; we hold at maximum until button
				// released):
				
				const float kVelScale = 0.012;
				scroll = (short) (-kVelScale * scrolledDist * dtMillis);  // <= 0

				if (scroll > -1)  // not enough time elapsed to scroll a pixel
				{					
					lastScrollMics = oldLastScrollMics;  // see note above
					trace("extending time on snapback, dtMilles = %l", dtMillis);
				}
			}
		}

		// now do the actual scrolling if necessary
				
		if (scroll)
		{
			// prevent overscrolling
			
			if (scrolledDist + scroll > scrollDistLimit)
			{
				scroll = scrollDistLimit - scrolledDist;
				vel = 0;
				//trace("would scroll past limit. scrolledDist %l scroll %l scrollDistLimit %l", scrolledDist, scroll, scrollDistLimit);
			}
			else if (scrolledDist + scroll < 0)
			{
				scroll = -scrolledDist;
				//trace("would snap back too far");
			}
			
			if (scroll)  // still want to scroll?
			{
				const short dv = gScrollingDV > 0 ? scroll : -scroll;

				gDoSimpleScroll = true;

				{
					Rect localRect = gScrollingRect;
					GlobalToLocalRectForPort(THEPORT_FROM_CURRENTA5, &localRect);
					CALL_ORIG_TRAP(ScrollRect) (&localRect, 0, dv, gJunkRgn);
				}
				
				gDoSimpleScroll = false;
				
				// if scrolling back the "right" way, restore other-side pixels if possible
			
				if (scroll < 0 && savedPixels)
					RestoreScrolledBackPixels(&offscreen, &offRect, scrolledDist, dv);
					
				if ((scrolledDist += scroll) > maxScrolledDist)
					maxScrolledDist = scrolledDist;  // remember biggest scroll so far
			}			
		}
		
		if (oldVel > 0 && vel <= 0)  // we had inertial vel but now about to snap back
		{
			vel = 0;
			
			if (Button())  // so we don't needlessly call Microseconds() again too soon
			{		
				//trace("max scrolled dist %d", scrolledDist);
				while (Button()) ;  // for NSB case; don't snap back til mouse is up
				UnhiliteControl(c);				
				lastScrollMics = Microseconds();  // in case we waited
			}
		}

		Assert(scrolledDist >= 0);
	} while (vel > 0 || scrolledDist);
	// TODO that "while" is infinite-looping if I drop into debugger sometimes
	// because scrolledDist is nonzero forever.  why???  excessive delay causing us to
	// scroll too much and overshoot somehow???  either way has never been a problem
	// in prod ...

	if (savedPixels)
	{
		UnlockPixels(GetGWorldPixMap(offscreen.g));
		CleanUpOffscreen(&offscreen);
	}
	
	if (!savedPixels || needFullUpdate)
	{
		// couldn't save/restore pixels, or restored some possibly-undrawn pixels
		// from behind a floating window; force app to redraw
		
		Rect invalR = gScrollingRect;
		GlobalToLocalRectForPort(THEPORT_FROM_CURRENTA5, &invalR);
		
		if (!needFullUpdate)
		{
			// invalidate based on biggest area scrolled out of bounds
			
			if (gScrollingDV > 0)  // restore pixels at bottom
				invalR.top = invalR.bottom - maxScrolledDist;
			else
				invalR.bottom = invalR.top + maxScrolledDist;
		}

		InvalRect(&invalR);
	}

	SetPort(savePort);

doneRubberBand:
	RestoreCursor();

	// ... that was kind of a lot
}

pascal short PatchedTrackControl(ControlHandle c, Point p, ActionProc action)
{
	short result = 0;
	Boolean didRubberBand = false;
	Boolean itsTheScrollBar = false;

	SetUpA4();

	trace("in patchedtrackcontrol");
	
	gNSBEligible = false;
	
	if ((**c).contrlOwner)  // edge case protection, we assume this is good below
	{	
		itsTheScrollBar = gWeAreScrolling && c == gTheScrollBar;
			   	
		// TODO check for local version of our click ?
		
		// if TrackControl calls ScrollRect, should we remember the scrolling rect?
	
		if (!gNSBLastFindControlPartCode)
		{
			// some apps like Resourcerer don't call FindControl before 
			// TrackControl ... weirdos!  we need a part code here so we do it ourselves
			ControlHandle dummy;
			gNSBLastFindControlPartCode = CALL_ORIG_TRAP(FindControl) (p, (**c).contrlOwner, &dummy);
			trace("getting part code late from FindControl direct");
		}
		
		gNSBEligible = 
				  (WindowPtr) FrontNonPaletteWindow() == (**c).contrlOwner  // rule out weird edge case?
			   && ControlIsAReasonableVerticalScrollBar(c)
			   // page would never trip NSB bounce since thumb is in way -- BUT setting
			   // this flag here lets us grab scroll rect in this case in 	rollRect
			   && PART_CODE_IS_ARROW_OR_PAGE(gNSBLastFindControlPartCode);  // don't do for thumb drags
	
		// there are two ways this routine could end up calling DoRubberBand():  either
		// we're doing a drag-scroll, or gNSBEligible is true.  either way, if the control
		// doesn't match the saved gNSBControl or has moved, clear the gNSB... data:
	
		if (itsTheScrollBar || gNSBEligible)
		{
			Boolean controlMoved;
			
			Rect globalControlRect = (**c).contrlRect;
			LocalToGlobalRectForPort((**c).contrlOwner, &globalControlRect);

			// did control move in global coordinates?
			controlMoved = !EqualRect(&globalControlRect, &gNSBControlRect);
			
			if (controlMoved
				&& EqualRect(&(**c).contrlRect, &gNSBLocalControlRect))
			{
				// control was just shifted in global coordiantes, but its local
				// coords are unchanged -- someone just moved its containing window?
				// just offset rects and we are good

				const short dh = globalControlRect.left - gNSBControlRect.left;
				const short dv = globalControlRect.top  - gNSBControlRect.top;

				gNSBControlRect = globalControlRect;
				OffsetRect(&gNSBScrollingRect, dh, dv);
				controlMoved = false;  // never mind the move, all good now
			}
			
			if (c != gNSBControl || controlMoved)
			{
				trace("setting gnsbcontrol to new control, got scrolling rect to false");
				// it's a different control than the last one --
				// wipe out whatever NSB control we'd previously noted, and note that
				// we don't yet have a scrollRect for this one:
				gNSBControl = c;
				gNSBGotScrollingRect = false;
				gNSBLastTimeSrcRectTop = 0;
				gNSBLastTimeBitsHeight = 0;
				
				gNSBControlRect = gNSBLocalControlRect = (**c).contrlRect;
				LocalToGlobalRectForPort((**c).contrlOwner, &gNSBControlRect);
			}
		}
	}
		
	if (itsTheScrollBar)
	{
		MouseCheckerResult r = kNoOp;
		GrafPtr savePort;
		Boolean btnDown;
		
		// maintain tiny FIFO queue of up to N recent mouse Y coords and times, used to
		// determine recent final average drag speed for later inertial scrolling

		enum { kMaxMouseYs = 3 };  // enum (not const int) so FIFO_BUF_SIZE is a valid array size below

		// stack storage for the queues to avoid need to call NewPtr
		char mouseYBuf[FIFO_BUF_SIZE(kMaxMouseYs)];
		char ticksBuf [FIFO_BUF_SIZE(kMaxMouseYs)];

		FIFO *q_mouseY = FIFO_New(kMaxMouseYs, mouseYBuf);
		FIFO *q_ticks  = FIFO_New(kMaxMouseYs, ticksBuf);
		
		const short startMouseY = gLastMouse.v;  // save this before MouseChecker() updates it
		short rowsScrolled = 0;	 // unsigned

		trace("it's the scroll bar");

		FIFO_Push(q_mouseY, startMouseY);
		FIFO_Push(q_ticks, gStartScrollTicks);
				
		GetPort(&savePort);
		SetPort((**c).contrlOwner);  // maybe not needed but TrackControl does this before calling action, so we do too

		p = gClickPt;				
					
		// note - MouseChecker could change gPartCode
		while (
				(btnDown = Button()) 
			 && (r = MouseChecker()) != kScrollNewPartCode)
		{
			// TODO do partial scrolling here too?

			// this is the main drag-scroll loop:
			// while user drags the mouse such that the synthetic part code is unchanged,
			// keep calling the action proc
		
			if (r != kNoOp)
			{
				gLastScrollTicks = TICKS;
				
				FIFO_Push(q_mouseY, MOUSE.v);
				FIFO_Push(q_ticks,  gLastScrollTicks);
			}
			trace("gGotScrollingRectAndDV is %d", gGotScrollingRectAndDV);

			if (r == kScrollSamePartCode
				// next line:  keep scrolling even if no mouse movement
		 		// if scrolling so far has undershot mouse move
				|| r == kNoOp 
					&& gGotScrollingRectAndDV
					&& (rowsScrolled + 1) * ABS(gScrollingDV) <= ABS(MOUSE.v - startMouseY))
			{
				const int oldVal = (**c).contrlValue;
				const Boolean atOrPastEnd =  
					   gPartCode == inUpButton   && oldVal <= (**c).contrlMin 
					|| gPartCode == inDownButton && oldVal >= (**c).contrlMax;

				if (!atOrPastEnd)  // normal drag-scrolling
				{
					trace("not at or past end");
					CallActionProc(c, action, gPartCode);
					
					if ((**c).contrlValue != oldVal)
						rowsScrolled++;
				}
				else
				{
					// user dragged past the end -- rubberband
					trace("dragged past end part %d old %d min %d max %d",
						gPartCode, oldVal, (**c).contrlMin, (**c).contrlMax);			

					do {
						DoRubberBand(c, 0, 0);

						// having rubber-banded past the end then come back, we now call 
						// into MouseChecker() in case user keeps going now in reverse 
						// direction with the mouse down -- in which case MouseChecker
						// will post a new click.  if user keeps going in SAME 
						// (bad) direction, we just call DoRubberBand() again.
						// first reset gLastMouse here so MouseChecker is comparing 
						// to latest post-rubberband starting mouse position:
						
						r = kNoOp;
						gLastMouse = MOUSE;
						
						while (Button() && (r = MouseChecker()) == kNoOp)
							;
					} while (r == kScrollSamePartCode);

					if (!Button())
						StopScrolling();
					
					// set flag to fall completely out of PatchedTrackControl; if user started
					// drag the other way, MouseChecker() just above will have posted a new 
					// click with a diffrent part code:

					didRubberBand = true;
				}
			}
		}

		if (!btnDown
			&& !didRubberBand
			&& r != kScrollNewPartCode
			&& FIFO_GetNumItems(q_ticks) > 0)
		{
			trace("button now up, !didRubberBand");
			// we broke out of the MouseChecker loop because the user relesed the button --
			// should we do inertial scrolling?
			
			if (TICKS - gLastScrollTicks <= MAX_INERTIAL_TICKS)
			{
				// we were scrolling at least slightly fast -- start inertial mode

				// inertial mode - keep control here in TrackControl til we're done
				// call actionProc on a timer 

				const int dv = MOUSE.v - FIFO_Oldest(q_mouseY);
				const int dt = TICKS   - FIFO_Oldest(q_ticks);

				if (dv != 0
						&& TICKS - FIFO_Newest(q_ticks) < 20)  // don't start inertial if mouse moving too slowly
					DoInertialMode(c, action, CalcInertialInterval(dt, dv));

				result = gPartCode;
			}

			StopScrolling();
		}
		
		SetPort(savePort);
		
		FIFO_Dispose(q_mouseY);
		FIFO_Dispose(q_ticks);
	}
	else
	{
		// itsTheScrollBar is false -- we aren't drag-scrolling, just check for 
		// NSB bounce at end if appropriate

		if (gNSBEligible)
		{
			// note to install our action proc which checks for a bounce when we hit 
			// the end, then calls the original action proc when done:
			gNSBOrigActionProc = action;
			action = NSBScrollBarAction;
			gNSBLastScrollMics = 0;
			gNSBDidntBounceYet = true;
		}
	
		// NOTE mouse click here must correspond to part code app saw when it called
		// TrackControl -- our FindControl patch should be ensuring this -- otherwise would be 
		// dangerous because action procs for thumb vs page regions/arrows take different
		// number of arguments!  can lead to bad stack & RTS to unimplemented instruction 
		// if we screw this up, so we are careful
		//	
		// also note, this one is a tail patch

		result = CALL_ORIG_TRAP(TrackControl) (c, p, action);
		
		gNSBOrigActionProc = NULL;
	}

	gNSBEligible = false;
	
	RestoreA4();

	// (should have always called real _TrackControl so action proc gets a chance 
	// to do things -- can't just return a fake part code here)
	return result;
}

pascal void NSBScrollBarAction(ControlHandle c, short partCode)
{
	SetUpA4();
	
	if (gNSBDidntBounceYet
		&& (  (partCode == inUpButton || partCode == inPageUp)
				&& (**c).contrlValue <= (**c).contrlMin 
		   || (partCode == inDownButton || partCode == inPageDown)
				&& (**c).contrlValue >= (**c).contrlMax))
	{
		// we define a standard NSB scroll bounce rubberband speed for "full-height"
		// scrollbar, then go slower for shorter scrollbars to prevent what looks
		// like overzealous bouncing:
		
		const long kPixelsPerSecondVelForStdHgt = 1000;
		const long kStdHgt = 350;

		short cHgt = (**c).contrlRect.bottom - (**c).contrlRect.top;
		long pixelsPerSecondVel;
		
		if (cHgt > kStdHgt) cHgt = kStdHgt;
		
		pixelsPerSecondVel = kPixelsPerSecondVelForStdHgt * cHgt / kStdHgt;

		// rubberband for an "NSB bounce" when user holds scroll button past the end
		DoRubberBand(c, pixelsPerSecondVel << 16  // convert to Fixed
									    >> 10,    // convert to pixels per ms
					 gNSBLastScrollMics);
		gNSBDidntBounceYet = false;
	}

	gNSBLastScrollMics = Microseconds();
	CallActionProc(c, gNSBOrigActionProc, partCode);
	
	RestoreA4();
}

pascal void PatchedSetCursor(CursPtr cursor)
{
	SetUpA4();
	
	if (gWeAreScrolling)
	{
		// do nothing -- don't let TrackControl() etc change to arrow cursor every time
		RestoreA4();
		return;
	}
	else
		RESTORE_A4_AND_JUMP_TO_TOOLTRAP(SetCursor);

	// never get here
	Fail("should never get here");
}

short DoScrollRectOrCopyBitsStuff(const Rect * const r, short *dv)
// assumes A4 already set up
//
// 'r' is in local coordinates of the current GrafPort (because either it's from
// ScrollRect, which requires this, or our CopyBits patch, which checks for this)
{
	short dvExtra = 0;

	if (gNSBEligible 
		&& PART_CODE_IS_ARROW_OR_PAGE(gNSBLastFindControlPartCode))
	{
		// save scrolling rect for normal scroll bounce case
		gNSBScrollingRect = *r;
		LocalToGlobalRectForPort(THEPORT_FROM_CURRENTA5, &gNSBScrollingRect);
		gNSBGotScrollingRect = true;
	}

	// TODO reconcile this condition with above 'if', should they just be the same?
	if (gWeAreInActionProc
		&& (gPartCode == inUpButton || gPartCode == inDownButton))
	{
		// app is calling us thru TrackControl action proc:  remember rect and dv
		gScrollingRect = *r;
		LocalToGlobalRectForPort(THEPORT_FROM_CURRENTA5, &gScrollingRect);

		if (!gScrollingDV || ABS(*dv) <	ABS(gScrollingDV))
			// some apps (THINK Reference) may change dv while scrolling, to always 
			// scroll one line (whose height may vary).  just above, we only allow
			// gScrollingDV to get smaller (in abs value) once set, so that we 
			// don't get confused by a taller line, then subsequently allow partial
			// scrolling to overshoot a shorter line height
			gScrollingDV = *dv;

		gGotScrollingRectAndDV = true;
		trace("setting gGotScrollingRectAndDV to true, gScrollingDV is %d", gScrollingDV);

		if (gPartiallyScrolled && *dv)
		{
			if (gPartiallyScrolled > 0 && gPartiallyScrolled > *dv
				|| gPartiallyScrolled < 0 && gPartiallyScrolled < *dv)
			{
				trace("whoaaaaa partial scroll overshoot, here's a negative true-up");
			}

			// we have previously partially scrolled by an amount <= dv in absolute value;
			// "true up":  reduce app's request by amount already partially scrolled
			if (gPartiallyScrolled)
			{
				trace("truing up, gPartiallyScrolled was %d, dv was %d", gPartiallyScrolled, *dv);

				*dv -= gPartiallyScrolled;
				dvExtra = gPartiallyScrolled;
				gPartiallyScrolled = 0;
			}
		}
	}
		
	return dvExtra;
}

pascal void PatchedCopyBits(BitMap *src, BitMap *dst, 
	const Rect * srcRect, const Rect * const dstRect,
	short mode, RgnHandle mask)
// someone call CopyBits - adjust behavior if we had already partially scrolled so the result
// isn't over-scrolling visually (among other things)
{
	SetUpA4();

	if (!gDoSimpleScroll
		&& dstRect->bottom - dstRect->top > 30)  // no tiny bitmaps	
	{
		// first check to see if we are copying to the current GrafPort's
		// portBits -- because CopyBits can be passed either a Bit/PixMap
		// or a pointer to a PixMapHandle, we need to check both what we've
		// got and what's in thePort in order to compare the correct addresses:
		
		const Ptr portBitsAddr = GET_GRAFPORT_BITORPIXMAP(THEPORT_FROM_CURRENTA5).baseAddr;
		Ptr dstBitsAddr = NULL;

		if ((dst->rowBytes & 0xC000) == 0xC000)
			// dst BitMap* is actually a pointer to a PixMapHandle
			dstBitsAddr = (***((PixMapHandle *) dst)).baseAddr;
		else
			// dst is a pointer to a BitMap or (if 1 hi bit of rowBytes is set -- but
			// we don't care here) PixMap whose baseAddr we can just grab
			dstBitsAddr = dst->baseAddr;

		if (dstBitsAddr == portBitsAddr)
		{
			// yes, we are copying to the current GrafPort's portBits -- possible this
			// is a scroll operation, let's dig in!
			
			short dv = 0;

			if (src == dst
				&& srcRect->left == dstRect->left
				&& srcRect->right == dstRect->right
				&& (dv = dstRect->bottom - srcRect->bottom) == dstRect->top - srcRect->top)
			{
				// normal CopyBits case:  using CopyBits like ScrollRect to shift
				// a bitmap vertically:
				
				short dvExtra;
				Rect fullRect = *srcRect;
				
				// quickie UnionRect of source and dest rects to get "full" scrolling rect 
				// for DoScroll... to remember if needed:
				
				if (dstRect->top < fullRect.top)
					fullRect.top = dstRect->top;
				else if (dstRect->bottom > fullRect.bottom)
					fullRect.bottom = dstRect->bottom;
	
				trace("calling doscroll from CB the 1st way w/ %r...", fullRect);		

				dvExtra = DoScrollRectOrCopyBitsStuff(&fullRect, &dv);
				
				if (dvExtra)
				{
					// we partially scrolled previously, so "true up" (shrink) this scroll
					// by offsetting the source rect (since contents of original srcRect
					// were already scrolled partially):
					gTempRect = *srcRect;
		
					gTempRect.bottom += dvExtra;
					gTempRect.top    += dvExtra;
		
					// don't want to change what the caller's Rect* points to since they
					// might be using it for other things -- so we change the argument on the
					// stack to give CopyBits a pointer to our global 
					srcRect = &gTempRect;
				}		
			}
			else if (gNSBControl)
			{
				// special case for apps like MacPaint that don't call ScrollRect AND don't
				// call CopyBits like ScrollRect but instead use it to blit from a buffer:
				// at least grab the scrolling rect if it looks like we are blitting
				// an appropriate-height bitmap to the main screen:
	
				const short bitsHgt = dstRect->bottom - dstRect->top;
				const short cntlHgt = (**gNSBControl).contrlRect.bottom - (**gNSBControl).contrlRect.top;
				
				if (ABS(bitsHgt - cntlHgt) <= 2)  // are we blitting ≈ height of scroll bar?
				{			
					const short yOffset = srcRect->top - gNSBLastTimeSrcRectTop;
	
					// are we blitting a same-height bitmap as last time with a slightly
					// different srcRect->top?  if so, save the change in srcRect->top
					// as a guess at DV:
	
					if (gWeAreInActionProc
						&& bitsHgt == gNSBLastTimeBitsHeight
						&& (gPartCode == inUpButton   && yOffset < 0 && yOffset > -50
						 || gPartCode == inDownButton && yOffset > 0 && yOffset < +50))
					{
						dv = -yOffset;
					}
	
					gNSBLastTimeBitsHeight = bitsHgt;
					gNSBLastTimeSrcRectTop = srcRect->top;
			
					trace("calling doscroll from CB ... the cheapo way w/ %r", *dstRect);		
					trace("    .... dst bitmap is %xl", dst);
	
					(void) DoScrollRectOrCopyBitsStuff(dstRect, &dv);
				}
			}
		}
	}
	
	RESTORE_A4_AND_JUMP_TO_TOOLTRAP(CopyBits);
}

pascal void PatchedScrollRect(const Rect * const r, short dh, short dv, RgnHandle rgn)
// during a single scroll sequence, note if ScrollRect is being called constantly
// (by patching it) with
// a single destRect.  if so, that's the scrolling area.  then between "slow" intertial
// timers, scroll that rect periodically by sub-line increments!  (always a whole fraction
// of a line).  when it's time to
// ask the app to scroll a full line, our patch just makes up the difference!  (always
// conclude scrolling with one of these "make-up" scrolls)
{
	short dvExtra = 0;
	
	SetUpA4();
	
	if (!dh)
	{
		trace("calling DoScroll from SR w/ %r", *r);
		dvExtra = DoScrollRectOrCopyBitsStuff(r, &dv);
	}	
	
	gDoSimpleScroll = true;  // don't double-up on DoScrollRect... from inside CopyBits!
	CALL_ORIG_TRAP(ScrollRect) (r, dh, dv, rgn);  // note this one is a tail patch
	gDoSimpleScroll = false;
	
	// if we reduced dv, now need to expand updateRgn back to full height
	// so that app draws full row of text (or whatever)
	if (dvExtra)
	{
		Rect extraRect = (**rgn).rgnBBox;
		
		if (dvExtra > 0)
			extraRect.bottom += dvExtra;  // scrolling up, lower bottom
		else
			extraRect.top += dvExtra;  // scrolling down, raise top
		
		RectRgn(rgn, &extraRect);  // assumes rectangular update rgn
	}
	
	RestoreA4();
}

void main(void)
{
	Ptr initPtr;		
	asm { move.l a0, initPtr }  // get pointer to ourselves
	
	RememberA0();	
	SetUpA4();

	try {
		//Handle settings;
		Handle initHndl;
		short resAttrs;

		//LoadAndDetachSettings(&failed);
		//if (failed) throw(__LINE__);
		
		//InstallGestaltSelector(&failed);	
		//if (failed) throw(__LINE__);
					
		initHndl = RecoverHandle(initPtr);  // get handle to ourselves
		resAttrs = GetResAttrs(initHndl);   // sanity check, ensure locked 
		Assert_THROWS(!ResError() && resAttrs & resSysHeap && resAttrs & resLocked, __LINE__);
		
		gGrabberCursorHndl = GetCursor(-4048);
		Assert_THROWS(gGrabberCursorHndl != NULL, __LINE__);
		resAttrs = GetResAttrs((Handle) gGrabberCursorHndl);   // sanity check
		Assert_THROWS(!ResError() && (resAttrs & resSysHeap), __LINE__);
	
		DetachResource(initHndl);  // we set Locked bit in "Set Project Type..." so don't need HLock
		DetachResource((Handle) gGrabberCursorHndl);	

		IN_SYS_HEAP(gJunkRgn = NewRgn());  // allocate a scrap RgnHandle in the system heap
		AssertMesg_THROWS(gJunkRgn, "out of memory", __LINE__);
		
		// patch traps
		
		INSTALL_PATCH(Tool, SystemEvent);
		INSTALL_PATCH(Tool, FindWindow);
		INSTALL_PATCH(Tool, FindControl);
		INSTALL_PATCH(Tool, TrackControl);
		INSTALL_PATCH(Tool, SetCursor);
		INSTALL_PATCH(Tool, ScrollRect);
		INSTALL_PATCH(Tool, CopyBits);

		// if (!**gSettings.active) throw(__LINE__);

		AssertMesg(CallShowInitIcon(-4048, GOOD_INIT_ICON), "out of memory?");
	} catch_all {
		// either load failed or **gSettings.active is false; call ShowInitIcon code X'ed-out:
		(void) CallShowInitIconXedOut(-4048, GOOD_INIT_ICON, X_ICON);
	} end_try;	

	RestoreA4();	
}
