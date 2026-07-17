/* auto-generated (do not modify): type=TEXT creator=KAHL hex=544558544B41484C000000000000000000000000000000000000000000000000 */
//==================================================================================
// CrutchUtilities.c
// ©2024 Steve Crutchfield
//
// A handy library of utilities for INITs, patches, and applications.
// Depends on the lower-level routines in CrutchError.c.
//
// I include this file and CrutchError.c directly in projects.  Precompiling a 
// library prevents THINK C's Smart Link option from working (though it doesn't seem
// to do much anyway...) and also prevents this code from seeing project-level
// settings like the APP_NAME #define.
//==================================================================================

#include <stdarg.h>
#include <QDOffscreen.h>
#include <Traps.h>
#include <Sound.h>
#include <Files.h>
#include <GestaltEqu.h>

#include "CrutchUtilities.h"

// TODO see IM:VI - Graphics Devices for some tips



// ========== QuickDraw utilities

short GetFontHeight(void)
{
	FontInfo f;
	GetFontInfo(&f);
	return f.ascent + f.descent + f.leading;
}

short EllipsifyString(const short leftEdge, const short rightEdge, Str255 s)
// shortens 's' with an ellipsis if needed such that, when drawn from 'leftEdge'
// in the current font/size, it will not extend past 'rightEdge'
//
// returns width of the resulting string
{
	short w;
	
	while (leftEdge + (w = StringWidth(s)) > rightEdge && s[0] > 2)
		s[--s[0]] = '…';  // truncate final 2 chars, add ellipsis
	
	return w;
}

// ========== Resource utilities

void GetCurResFileInfo(Str255 name, short *wdRefNum)
// get filename and vRefNum for CurResFile(), use e.g. to save
// info about INIT file itself at startup time for later resource
// loading if needed
//
// note:  PBOpenWD reuses an existing working directory when vRefNum, dirID, and
// procID all match (see IM: Files), so the nonzero signature procID below makes
// repeated calls for the same directory return the same WD refnum rather than
// leaking a new one each call
{
	FCBPBRec fcb_pb;
	WDPBRec  wd_pb;

	fcb_pb.ioRefNum  = CurResFile();
	fcb_pb.ioNamePtr = name;
	fcb_pb.ioFCBIndx = 0;
	fcb_pb.ioVRefNum = 0;

	CheckFatal(
		PBGetFCBInfo(&fcb_pb, false));
	
	wd_pb.ioWDProcID = 'Crut';  // nonzero signature --> WD gets reused (see note above)
	wd_pb.ioNamePtr  = NULL;
	wd_pb.ioWDDirID  = fcb_pb.ioFCBParID;
	wd_pb.ioVRefNum  = fcb_pb.ioFCBVRefNum;

	CheckFatal(
		PBOpenWD(&wd_pb, false));
	
	*wdRefNum = wd_pb.ioVRefNum;
}

Handle GetResourceInSysHeap(OSType type, short id)
{
	const THz saveZone = TheZone;
	Handle h;	

	TheZone = SysZone;
	h = GetResource(type, id);
	TheZone = saveZone;

	return h;
}

Boolean ReloadINITResource(Handle *h, OSType type, short id, Str31 fName, short wdRefNum)
// check to see if a resource from our INIT file was purged (or never loaded); 
// if yes, reopen INIT resource fork and reload, confirming it goes into sysHeap,
// then detach.
// (can't just use LoadResource when later running INIT code, since our resource
// file was closed at startup time.)
//
// return true if successful.
{
	if (!*h || !**h)  // was resource never loaded, or purged?  (if not, do nothing)
	{
		short saveVol, rfRefNum;
		
		if (Check(GetVol(NULL, &saveVol)))
		{
			if (*h)
				DisposeHandle(*h);  // clean up old purged handle (not a resource anymore)

			*h = NULL;
				
			SetVol(NULL, wdRefNum);
		
			if (Assert((rfRefNum = OpenResFile(fName)) != -1))
			{
				if (ConfirmResourceWithFlags(*h = Get1Resource(type, id), resSysHeap))
					DetachResource(*h);
					
				CloseResFile(rfRefNum);
			}
			
			SetVol(NULL, saveVol);
		}
	}
	
	return *h && **h;
}

// ============ Environment information utilities

Boolean FindProcess(OSType type, OSType creator, ProcessSerialNumber *psn)
// find PSN for given creator/type, return false if not found
{
	psn->highLongOfPSN = kNoProcess;
	psn->lowLongOfPSN  = kNoProcess;

	while (noErr == GetNextProcess(psn))
	{
		ProcessInfoRec pInfo;
		Str31 pName;
		
		pInfo.processInfoLength = sizeof(ProcessInfoRec);
		pInfo.processName       = pName;
		pInfo.processAppSpec    = NULL;

		if (noErr == GetProcessInformation(psn, &pInfo)
			&& pInfo.processSignature == creator
			&& pInfo.processType      == type)
		{
			return true;
		}
	}
	
	return false;
}

Boolean FrontProcessIs(ProcessSerialNumber psn)
{
	ProcessSerialNumber frontPsn;
	Boolean same;
		
	return noErr == GetFrontProcess(&frontPsn) 
		&& noErr == SameProcess(&frontPsn, &psn, &same)
		&& same;
}

void GetAppInfo(ProcessSerialNumber psn, OSType *type, OSType *creator)
// get app's type and creator code from PSN
{	
	ProcessInfoRec pir;

	pir.processInfoLength = sizeof(ProcessInfoRec);
	pir.processName 	  = NULL;
	pir.processAppSpec 	  = NULL;
	
	AssertMesgReturn(TrapAvailable(_OSDispatch), "System 7 or MultiFinder required", );
	
	Check(GetProcessInformation(&psn, &pir));
	
	*type    = pir.processType;
	*creator = pir.processSignature;	
}

short GetVers1(unsigned char *s)
// get info from current resource file's 'vers' 1 resource
//
// 		s:  		if non-NULL, gets short version string, 
//					or empty string if no resource found
//
// 		returns:	first 2 bytes of vers resource, e.g. 0x0601 = 6.0.1,
//					or 0 if no resource found
//
{
	const short saveResLoad = ResLoad;
	Handle versResource;
	Boolean dontReleaseResource = false;

	// first see if the app (assuming we're in an INIT here) had already loaded its
	// own 'vers' resource into memory for some reason -- if so, we mustn't call
	// _ReleaseResource on it later!

	SetResLoad(false);
	versResource = Get1Resource('vers', 1);
	
	if (versResource && *versResource)
		// it was already loaded by the app -- don't release it later!
		dontReleaseResource = true;
	else
	{
		SetResLoad(true);
		versResource = Get1Resource('vers', 1);
	}
	
	SetResLoad(saveResLoad);  // in case the app had used SetResLoad(false) for some reason??
	
	if (ResError() || !versResource)
	{
		if (s) s[0] = 0;
		return 0;
	}
	else
	{
		// get version word, e.g. 6.0.1 --> 0x0601
		short vWord = *((short *) *versResource);
		
		if (s)
		{
			// get pointer short version string and copy it into s
			const unsigned char * const vStr = (unsigned char *) (*versResource + 6);
			CopyStr(vStr, s);
		}
		
		if (!dontReleaseResource)
			ReleaseResource(versResource);

		return vWord;
	}
}

// =========== Syntactic sugar for ShowInitIcon (linked separately)

bool CallShowInitIcon(short code_id, short icon_id) {
	const Handle showIconCode = Get1Resource('Code', code_id);
	if (!showIconCode) return false;
	HLock(showIconCode);
	((pascal void (*) (short, Boolean)) *(showIconCode)) ((icon_id), true);
	HUnlock(showIconCode);
	return true;
}

bool CallShowInitIconXedOut(short code_id, short icon_id, short x_id) {
	const Handle showIconCode = Get1Resource('Code', code_id);
	if (!showIconCode) return false;
	HLock(showIconCode);
	((pascal void (*) (short, Boolean)) *(showIconCode)) ((icon_id), false);
    ((pascal void (*) (short, Boolean)) *(showIconCode)) ((x_id), true);
    HUnlock(showIconCode);
	return true;
}

// ========== Sound utilities

short SoundManagerVersion(void)
// note:  Sound Manager 3.0 comes with System 7.1 (built-in), or 7.0 w/ QuickTime;
// great article by its author Jim Reekes here:
// http://preserve.mactech.com/articles/develop/issue_16/034-038_QuickTime_column.html
{
	if (TrapAvailable(_SoundDispatch))
		return SndSoundManagerVersion().majorRev;
	else
		return 0;
}

// ========== Debug utilities

#ifdef DEBUG

void DebugRect(const Rect * const r)
{
	Debug("%r", *r);
}

#endif

// ========== FIFO - a mini FIFO queue of longs; could of course be ptrs to structs too
//
// (note this fully allocates space for max # entries, intended for small queues only)

FIFO *FIFO_New(short maxItems, void *storage)
// storage:  if non-NULL, must point to FIFO_BUF_SIZE(maxItems) bytes, which are
//           used to hold the queue (and FIFO_Dispose then frees nothing) -- lets
//           callers use e.g. stack storage inside a trap patch, where a failed
//           NewPtr here would mean a NULL deref scribbling on low memory
//
// returns NULL if storage == NULL and NewPtr fails
{
	FIFO *q;

	if (storage)
	{
		q = (FIFO *) (((long) storage + 1) & ~1L);  // 68000 requires even alignment
		q->items = (long *) (q + 1);  // items live just past the struct itself
		q->ownsStorage = false;
	}
	else
	{
		q = (FIFO *) NewPtr(sizeof(FIFO));
		
		if (!q)
			return NULL;
		
		q->items = (long *) NewPtr(sizeof(long) * maxItems);
		
		if (!q->items)
		{
			DisposePtr((Ptr) q);
			return NULL;
		}
		
		q->ownsStorage = true;
	}

	q->numItems = 0;
	q->maxItems = maxItems;

	return q;
}

void FIFO_Dispose(FIFO *q)
{
	if (q->ownsStorage)
	{
		DisposePtr((Ptr) q->items);
		DisposePtr((Ptr) q);
	}
}

long FIFO_Newest(FIFO *q)
{
	AssertMesgFatal(q->numItems > 0, "attempt to get newest element from empty queue");
	return q->items[q->numItems - 1];
}

long FIFO_Oldest(FIFO *q)
{
	AssertMesgFatal(q->numItems > 0, "attempt to get oldest element from empty queue");
	return q->items[0];
}

void FIFO_Push(FIFO *q, long newItem)
// add to FIFO, dropping oldest if full
{
	if (q->numItems >= q->maxItems)
	{	
		// we're full; push everything back, dropping oldest
		short i;
		
		for (i = 0; i < q->maxItems - 1; i++)
			q->items[i] = q->items[i + 1];
		
		q->numItems = q->maxItems - 1;
	}

	q->items[q->numItems++] = newItem;
}

// ========== LocalToGlobal- and GlobalToLocal-related routines

/*
Point GetGlobalCoordOffset(GrafPtr g)
{

	if (g->portBits.rowBytes & 0xC000)
		// not OK at interrupt time, handle might be unlocked 
		return topLeft((**((CGrafPtr) g)->portPixMap).bounds);
	else
		return topLeft(g->portBits.bounds);		

		
	return topLeft(GET_GRAFPORT_BITORPIXMAP(g).bounds);
}
*/

void GlobalToLocalForPort(GrafPtr g, Point *p)
// allows a quick GlobalToLocal without changing port
{
	Point offset = GET_GLOBAL_COORD_OFFSET(g);
		
	p->h += offset.h;
	p->v += offset.v;
}

void LocalToGlobalForPort(GrafPtr g, Point *p)
// allows a quick LocalToGlobal without changing port
{
	Point offset = GET_GLOBAL_COORD_OFFSET(g);
		
	p->h -= offset.h;
	p->v -= offset.v;
}

void LocalToGlobalRect(Rect *r, Point offset)
{
	r->left   -= offset.h;
	r->top    -= offset.v;
	r->right  -= offset.h;
	r->bottom -= offset.v; 
}

void GlobalToLocalRectForPort(GrafPtr g, Rect *r)
{
	Point offset = GET_GLOBAL_COORD_OFFSET(g);
		
	r->left   += offset.h;
	r->top    += offset.v;
	r->right  += offset.h;
	r->bottom += offset.v;
}

void LocalToGlobalRectForPort(GrafPtr g, Rect *r)
{
	Point offset = GET_GLOBAL_COORD_OFFSET(g);
		
	r->left   -= offset.h;
	r->top    -= offset.v;
	r->right  -= offset.h;
	r->bottom -= offset.v;
}

void GlobalToLocalRect(Rect *r, Point offset)
{
	r->left   += offset.h;
	r->top    += offset.v;
	r->right  += offset.h;
	r->bottom += offset.v; 
}

Rect GetGlobalWindowPortRect(WindowPtr w)
// return a window's portRect in global coords
{
	Rect r = w->portRect;
	LocalToGlobalRectForPort(w, &r);
	return r;
}

Rect GetGlobalWindowStrucRect(WindowPtr w)
// return the rect enclosing a window's strucRgn, in global coords --
// note this may temporarily show/hide the window offscreen, invoking calls to its WDEF
{
	// get global coords of w's topleft corner
	
	Point corner = topLeft(w->portRect);
	Rect r;
	
	LocalToGlobalForPort(w, &corner);

	if (((WindowPeek) w)->visible)
		r = (**(((WindowPeek) w)->strucRgn)).rgnBBox;
	else
	{
		// get w's global structure rect while still hidden by moving it way offscreen
		// and temporarily showing it
		// per Scott Knaster, Macintosh Programming Secrets, p. 329		
		const short kOffscreenShift = 0x4000;
		MoveWindow(w, corner.h, corner.v + kOffscreenShift, false);
		ShowHide(w, true);
		r = (**(((WindowPeek) w)->strucRgn)).rgnBBox;
		ShowHide(w, false);
		MoveWindow(w, corner.h, corner.v, false);
		
		r.top    -= kOffscreenShift;
		r.bottom -= kOffscreenShift;
	}
		
	return r;
}

// ========== Timer routines

/*
static void TimerDone(void)
{  // NOT NEEDED - see RUNNING(t) macro
	// (requires System 6.0.3 (?) to get TMTask address in A1)
	asm { sf Timer.running(a1) }  // t->running = false
}
*/

void SetUpTimer(Timer *t)
{
	t->task.qType	   = 0;
	t->task.tmAddr     = NULL;  // needs revised time mgr  // (ProcPtr) TimerDone;
	t->task.tmCount    = 0;
	t->task.tmWakeUp   = 0;
	t->task.tmReserved = 0;

	InsTime((QElemPtr) t);
}

void StartTimer(Timer *t, long count)
{
	PrimeTime((QElemPtr) t, count);
}

void StopTimer(Timer *t)
{
	RmvTime((QElemPtr) t);
	InsTime((QElemPtr) t);
}

void CleanUpTimer(Timer *t)
{
	RmvTime((QElemPtr) t);
}

void MiniDelay(const long dt)
// TODO check for/needs revised or extended Time Mgr
// 	dt:  positive --> milliseconds, negative --> microseconds
{
	long zero[5];  // TMTask is 20 bytes == 5 longs
	const QElemPtr t = (QElemPtr) &zero;
	
	// do this instead of using an initializer which would reference A4/A5
	zero[0] = zero[1] = zero[2] = zero[3] = zero[4] = 0L;
	
	InsTime(t);
	PrimeTime(t, dt);
	
	while (t->qType & 0x8000)
		;
	
	RmvTime(t);
}

// =========== Offscreen bitmaps/pixmaps

Boolean SetUpOffscreen(Offscreen *o, const Rect * const r, short depth, const long flags)
// 	depth == 0:  optimize for screen copying to global rect r (per NewGWorld's use of depth)
// 	flags:  a bitfield, can contain USE_SYS_ZONE, TRY_TEMP_MEM, PURGEABLE
// 	returns:  true if successful, false if any error
{
	const THz saveZone = TheZone;
	long qdVers = 0;
	long adjFlags = 0;

	// check QuickDraw version
	// TODO add stuff for orig B&W QD

	CheckReturn(Gestalt(gestaltQuickdrawVersion, &qdVers), false);
	AssertReturn(qdVers >= gestalt32BitQD, false);
		
	// set up offscreen stuff

	if (flags & USE_SYS_ZONE)
		TheZone = SysZone;

	if (flags & PURGEABLE)
		adjFlags |= pixPurge;

	if (flags & TRY_TEMP_MEM) 
	{
		// if caller requested temp mem, try that first -- if it fails due to running
		// out of temp mem, try again in regular memory -- this MIGHT not be necessary
		// but I'm unclear on exactly what the memory manager does here
		//
		// note pre-Sys 7, special routines are needed to manipulate temp mem so
		// this might not always be a great idea ... see THINK Ref entry on temporary
		// memory
		OSErr newGWorldErr = 
			NewGWorld(&o->g, depth, r, NULL, NULL, adjFlags | useTempMem);

		if (newGWorldErr == cTempMemErr)
			// try again without temp mem flag
			newGWorldErr = NewGWorld(&o->g, depth, r, NULL, NULL, adjFlags);

		TheZone = saveZone;
		CheckMesgReturn(newGWorldErr, "probably out of memory", false);
	}
	else
	{
		// not trying to use temp mem -- just try NewGWorld normally
		const OSErr newGWorldErr = NewGWorld(&o->g, depth, r, NULL, NULL, adjFlags);

		TheZone = saveZone;
		CheckMesgReturn(newGWorldErr, "probably out of memory", false);
	}
	
	o->inited = true;
	
	{
		GDHandle saveDevice;
		CGrafPtr savePort;

		GetGWorld(&savePort, &saveDevice);		
		
		AssertFatal(LockPixels(GetGWorldPixMap(o->g)));  // should never fail for nonpurgeable pixmaps
		SetGWorld(o->g, NULL);

		if (!depth)
			// depth == 0 mode has NewGWorld give us a portRect starting at (0, 0) and size-optimized
			// for CopyBits ... this is great, except we want portRect to align with 'r'.
			// adjust topleft corner here to fix that:
			SetOrigin(r->left, r->top);		

		RGB_BW();

		ClipRect(&o->g->portRect);
		EraseRect(&o->g->portRect);
		
		SetGWorld(savePort, saveDevice);
		UnlockPixels(GetGWorldPixMap(o->g));
	}
	
	return true;
}

void CleanUpOffscreen(Offscreen *o)
{
	if (o->inited)  // (else silently do nothing, so harmless if initialization failed)
	{
		DisposeGWorld(o->g);	
		o->inited = false;
	}
}

void SavePixelsWithOffset(const Offscreen * const o, const Rect * const r, const short dh, const short dv)
// no need to call LockPixels, we do it and restore locked/unlocked state when done
// Rect is in GLOBAL coords
// 'r' is source rect; offsets dh, dv apply to destination rect only
// we avoid using thePort here (A5 might not be set up)
{
	const BitMap *from = &(COLOR_QD ? (GrafPtr) WMgrCPort : WMgrPort)->portBits;
	const BitMap *to   = (BitMap *) *GetGWorldPixMap(o->g); // &((GrafPtr) o->g)->portBits;
	GDHandle saveDevice;
	CGrafPtr savePort;
	const Boolean alreadyLocked = GWORLDPIXELS_LOCKED(o->g);

	AssertMesgReturn(o->inited, "tried to save pixels to an un-inited Offscreen", );
	
	if (!alreadyLocked) Assert(LockPixels(GetGWorldPixMap(o->g)));  // should never fail for nonpurgeable pmap
	
	// set to offscreen GWorld -- we don't really need this for CopyBits, except 
	// that if current port has weird fore/backcolor it would colorize the image 
	// (we set ours to black/white in SetUpOffsreen earlier)
	GetGWorld(&savePort, &saveDevice);
	SetGWorld(o->g, NULL);

	if (dh || dv)
	{
		Rect destRect = *r;
		destRect.top    += dv;
		destRect.bottom += dv;
		destRect.left   += dh;
		destRect.right  += dh;
		CopyBits(from, to, r, &destRect, srcCopy, NULL);
	}
	else
		CopyBits(from, to, r, r, srcCopy, NULL);

	SetGWorld(savePort, saveDevice);
	
	if (!alreadyLocked) UnlockPixels(GetGWorldPixMap(o->g));
}

void RestorePixelsWithOffset(const Offscreen * const o, const Rect * const r, const short dh, const short dv)
// blits pixels from the given Offscreen to the screen -- need not be paired with
// SavePixels, for example to do double buffering:
//
//	SetUpOffscreen(drawBuffer, ...);
//	BlitOffscreen(backgroundPixels /* previously saved */, drawBuffer, r);
//	BeginOffscreen(drawBuffer);
//		... (draw on top of background pixels)
//	EndOffscreen(drawBuffer);
//	RestorePixels(drawBuffer, r);  // blit to screen
//
// no need for caller to call LockPixels, we do it and restore locked/unlocked 
// 		state when done
// Rect is in GLOBAL coords
// 'r' is source rect; offsets dh, dv apply to destination rect only
// we avoid using thePort here (A5 might not be set up)
{
	const BitMap *from   = (BitMap *) *GetGWorldPixMap(o->g);  // &((GrafPtr) o->g)->portBits;
//	const BitMap *to     = &(COLOR_QD ? (GrafPtr) WMgrCPort : WMgrPort)->portBits;
	const Boolean alreadyLocked = GWORLDPIXELS_LOCKED(o->g);

	CGrafPtr savePort;
	GDHandle saveDevice;
	CGrafPort screenPort;
	RgnHandle oldRgn;
	
	AssertMesgReturn(o->inited, "tried to restore pixels from an un-inited Offscreen", );

	if (!alreadyLocked) AssertFatal(LockPixels(GetGWorldPixMap(o->g)));  // should never fail for nonpurgeable pmap

	// TODO check for 32b color QD

	GetGWorld(&savePort, &saveDevice);
	OpenCPort(&screenPort);

	// cheapo set clipRgn to GrayRgn so we don't draw over rounded screen corners --
	// we do this to avoid SetClip() which makes a copy of the region.
	// (using GrayRgn like this prevents restoring over the menu bar -- if this is a problem
	// would have to change this approach)
	oldRgn = screenPort.clipRgn;
	screenPort.clipRgn = GrayRgn;

	SetGWorld(&screenPort, MainDevice);
	
	SAVE_RGB {
		RGB_BW();

		if (dh || dv)
		{
			Rect destRect = *r;
			destRect.top    += dv;
			destRect.bottom += dv;
			destRect.left   += dh;
			destRect.right  += dh;
			CopyBits(from, &((GrafPtr) &screenPort)->portBits, r, &destRect, srcCopy, NULL);
		}
		else
			CopyBits(from, &((GrafPtr) &screenPort)->portBits, r, r, srcCopy, NULL);
	} RESTORE_RGB;
	
	SetGWorld(savePort, saveDevice);
	screenPort.clipRgn = oldRgn;  // put back so GrayRgn doesn't get nuked below!
	CloseCPort(&screenPort);

	if (!alreadyLocked) UnlockPixels(GetGWorldPixMap(o->g));
}

void BlitOffscreenWithOffset(const Offscreen * const oFrom, Offscreen * const oTo, 
	const Rect * const r, const short dh, const short dv)
// no need to call LockPixels, we do it and restore locked/unlocked state when done
// for both bitmaps
// 'r' is source rect; offsets dh, dv apply to destination rect only
// Rect is in GLOBAL coords
{
	const BitMap *from = (BitMap *) *GetGWorldPixMap(oFrom->g);
	const BitMap *to   = (BitMap *) *GetGWorldPixMap(oTo->g);

	const Boolean alreadyLockedFrom = GWORLDPIXELS_LOCKED(oFrom->g);
	const Boolean alreadyLockedTo   = GWORLDPIXELS_LOCKED(oTo->g);

	if (!alreadyLockedFrom) AssertFatal(LockPixels(GetGWorldPixMap(oFrom->g)));  // should never fail
	if (!alreadyLockedTo)   AssertFatal(LockPixels(GetGWorldPixMap(oTo->g)));	 // for nonpurgeable pmap
	
	// set to offscreen GWorld -- we don't really need this for CopyBits, except 
	// that if current port has weird fore/backcolor it would colorize the image 
	// (we set ours to black/white in SetUpOffsreen earlier)
	BeginOffscreen(oTo);
	if (dh || dv)
	{
		Rect destRect = *r;
		destRect.top    += dv;
		destRect.bottom += dv;
		destRect.left   += dh;
		destRect.right  += dh;
		CopyBits(from, to, r, &destRect, srcCopy, NULL);
	}
	else
		CopyBits(from, to, r, r, srcCopy, NULL);
	EndOffscreen(oTo);

	if (!alreadyLockedFrom) UnlockPixels(GetGWorldPixMap(oFrom->g));
	if (!alreadyLockedTo)   UnlockPixels(GetGWorldPixMap(oTo->g));
}

void BeginOffscreen(Offscreen * const o)
// note:  this leaves pixels locked
{
	AssertMesgReturn(o->inited, "tried to BeginOffscreen on an un-inited Offscreen", );

	o->wasLockedBeforeBeginOffscreen = GWORLDPIXELS_LOCKED(o->g);

	if (!o->wasLockedBeforeBeginOffscreen) AssertFatal(LockPixels(GetGWorldPixMap(o->g)));  // should never fail for nonpurgeable pmap
	
	GetGWorld(&o->savePort, &o->saveDevice);
	SetGWorld(o->g, NULL);
	
	o->calledBeginOffscreen = true;
}

void EndOffscreen(Offscreen * const o)
// note:  this restores original pixel locked/unlocked state from when BeginOffscreen
// was called
{
	AssertMesgReturn(o->inited, "tried to EndOffscreen on an un-inited Offscreen", );
	AssertMesgReturn(o->calledBeginOffscreen, "tried to EndOffscreen without BeginOffscreen", );

	SetGWorld(o->savePort, o->saveDevice);

	if (!o->wasLockedBeforeBeginOffscreen) UnlockPixels(GetGWorldPixMap(o->g));
	
	o->calledBeginOffscreen = false;
}

// ========== BTMP resource utilities

// Creates a BTMP resource from something on screen 
// (mainly intended during dev time)
void CreateBTMPResourceFromScreen(const Rect * const r, const short id, Str255 resName)
{
	const BitMap *from = (BitMap *) *(**MainDevice).gdPMap;
	Handle h;
	BTMP *btmp;

	const int rowBytes = BITS_ROWBYTES(r->right - r->left);	

	const long btmpSize = sizeof(BitMap) + rowBytes * (r->bottom - r->top);
 	btmp = (void *) NewPtr(btmpSize);
	
	btmp->bitMap.bounds   = *r;
	btmp->bitMap.rowBytes = rowBytes;
	btmp->bitMap.baseAddr = (Ptr) &btmp->bits;
	
	CopyBits(from, &btmp->bitMap, r, r, srcCopy, NULL);
	
	btmp->bitMap.baseAddr = NULL;  // must be populated on load
	
	if (CheckFatal(PtrToHand(&btmp->bitMap, &h, btmpSize)))
	{
		AddResource(h, 'BTMP', id, resName);
		
		if (CheckFatal(ResError()))
		{
			WriteResource(h);
			CheckFatal(ResError());
		}

		DisposeHandle(h);		
	}
}

void PlotBTMP(BTMP ** const btmp, const BitMap * dest, const Rect * const r, 
	const short mode, const RgnHandle mask)
// dest == NULL:  copy to MainDevice
{
	if (!dest)
		dest = (BitMap *) *(**MainDevice).gdPMap;

	HLock((Handle) btmp);
	(**btmp).bitMap.baseAddr = (Ptr) &(**btmp).bits;

	CopyBits(&(**btmp).bitMap, dest, &(**btmp).bitMap.bounds, r, mode, mask);

	(**btmp).bitMap.baseAddr = NULL;	
	HUnlock((Handle) btmp);
}

void PlotBTMPAtXY(BTMP ** const btmp, const BitMap * dest, const short x, const short y, 
	const short mode, const RgnHandle mask)
{
	Rect r;
	const Rect * const bounds = &(**btmp).bitMap.bounds;
	
	r.top    = y;
	r.left   = x;
	r.bottom = y + bounds->bottom - bounds->top;
	r.right  = x + bounds->right  - bounds->left;
	
	PlotBTMP(btmp, dest, &r, mode, mask);
}

// ========== Saving pixels under a window

Offscreen *SavePixelsUnderWindow(WindowPtr w, long flags)
// 	flags:  same as for SetUpOffscreen(), above
{
	Offscreen *offscreen = (Offscreen *) NewPtr(sizeof(Offscreen));
	Rect r;
	Point corner;
	
	if (!Assert(offscreen))
		return NULL;

	AssertMesg(!((WindowPeek) w)->visible, "saving pixels but new window already shown?");
	
	r = GetGlobalWindowStrucRect(w);  // note this temp shows/hides the window offscreen
	SectRect(&r, &WMgrPort->portRect, &r);

	if (  !EmptyRect(&r)
		&& SetUpOffscreen(offscreen, &r, 0, flags)
		&& LockPixels(GetGWorldPixMap(offscreen->g)))
	{
		SavePixels(offscreen, &r);
		UnlockPixels(GetGWorldPixMap(offscreen->g));
	}
	else
	{
		// empty rect, or failed -- 
		// RestorePixels...() will notice NULL pointer and do nothing;
		// normal updating will happen eventually
		DisposePtr((Ptr) offscreen);
		offscreen = NULL;
	}

	return offscreen;
}

void RestorePixelsUnderWindow(Offscreen *offscreen)
{
	// TODO NEW:  if LockPixels fails below (e.g. pixMap was purged), we correctly
	// skip the restore but never CleanUpOffscreen/DisposePtr, so the GWorld and the
	// Offscreen struct leak

	if (offscreen  // (if NULL, something went wrong in setup, just use normal updating)
		&& LockPixels(GetGWorldPixMap(offscreen->g)))  // (if failed, pixMap maybe purged, use normal updating)
	{
		WindowPtr w;
		Rect offRect = offscreen->g->portRect;		
		GrafPtr savePort;

		RestorePixels(offscreen, &offRect);

		GetPort(&savePort);
		
		// validate any front-app windows that intersect the rect
		for (w = FrontWindow(); w; w = (WindowPtr) ((WindowPeek) w)->nextWindow)
		{
			Rect r = offRect;
			GlobalToLocalRectForPort(w, &r);
			SectRect(&r, &w->portRect, &r);
			SetPort(w);
			ValidRect(&r);
		}
		
		SetPort(savePort);
		
		CleanUpOffscreen(offscreen);
		DisposePtr((Ptr) offscreen);
	}
}

// =========== InitManagers

static void _DefaultResumeProc(void);

static void _DefaultResumeProc(void)
{
	ExitToShell();
}

void InitManagers(ProcPtr resumeProc)
{
	MoreMasters();
	
	InitGraf(&thePort);
	InitFonts();
	InitWindows();
	InitMenus();
	TEInit();
	InitDialogs(resumeProc ? resumeProc : _DefaultResumeProc);
	InitCursor();
	FlushEvents(everyEvent, 0);
}

// ========== Hide/ShowMenuBar

// These are simple versions from Knaster, Macintosh Programming Secrets (2e0,
// p. 492ff.  I did much more elaborate versions for Exposé, see that code for details.

void HideMenuBar(HideMenuBarData *data, Boolean allowClicks)
// data:  a pointer to a place to store things we should remember,
// used so we don't make any assumptions about A5 globals here etc.
// must be initialized (all zeros is fine) before first use.
{
	if (!data->hidden)
	{
		const RgnHandle menuRgn = NewRgn();
		const WindowPeek frontW = (WindowPeek) FrontWindow();
		
		data->oldMBarHeight = MBarHeight;
		
		if (!allowClicks)  // set allowClicks to let user pull down invisible menus
			MBarHeight = 0;
		
		if (!data->oldGrayRgn)
			data->oldGrayRgn = NewRgn();
		
		CopyRgn(GrayRgn, data->oldGrayRgn);
		
		{
			Rect r = (**MainDevice).gdRect;
			r.bottom = data->oldMBarHeight;
			RectRgn(menuRgn, &r);
		}
		
		UnionRgn(GrayRgn, menuRgn, GrayRgn);
		PaintBehind(frontW, menuRgn);
		CalcVisBehind(frontW, menuRgn);
		DisposeRgn(menuRgn);
		
		data->hidden = true;
	}
}

void ShowMenuBar(HideMenuBarData *data)
{
	if (data->hidden)
	{
		const WindowPeek frontW = (WindowPeek) FrontWindow();

		MBarHeight = data->oldMBarHeight;
		
		AssertMesgReturn(data->oldGrayRgn, "oldGrayRgn is NULL in ShowMenuBar", );
		CopyRgn(data->oldGrayRgn, GrayRgn);
		
		{
			// repurpose oldGrayRgn for a sec to hold the new menu bar region
			Rect r = (**MainDevice).gdRect;
			r.bottom = data->oldMBarHeight;
			RectRgn(data->oldGrayRgn, &r);
		}

		PaintBehind(frontW, data->oldGrayRgn);
		CalcVisBehind(frontW, data->oldGrayRgn);

		DrawMenuBar();
	
		data->hidden = false;
		DisposeRgn(data->oldGrayRgn);	
		data->oldGrayRgn = NULL;
	}
}
