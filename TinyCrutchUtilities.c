/* auto-generated (do not modify): type=TEXT creator=KAHL hex=544558544B41484C000000000000000000000000000000000000000000000000 */
//==================================================================================
// TinyCrutchUtilities.c
// ©2024 Steve Crutchfield
//
// A STRIPPED COPY of CrutchUtilities.c for the TouchScroll INIT project:  only
// functions TouchScroll reaches (directly or transitively) are kept, because the
// THINK/Symantec 'Smart Link' option doesn't reliably strip dead code.  If you
// need something that isn't here, copy it back in from CrutchUtilities.c -- and
// don't fix a bug here without also fixing it in CrutchUtilities.c!
//
// Depends on the lower-level routines in TinyCrutchError.c.
//==================================================================================

#include <stdarg.h>
#include <QDOffscreen.h>
#include <Traps.h>
#include <GestaltEqu.h>

#include "CrutchUtilities.h"

// TODO see IM:VI - Graphics Devices for some tips



// ============ Environment information utilities

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
	((pascal void (*) (short, Boolean)) *(showIconCode)) ((icon_id), true);
	return true;
}

bool CallShowInitIconXedOut(short code_id, short icon_id, short x_id) {
	const Handle showIconCode = Get1Resource('Code', code_id);
	if (!showIconCode) return false;
	((pascal void (*) (short, Boolean)) *(showIconCode)) ((icon_id), false);
    ((pascal void (*) (short, Boolean)) *(showIconCode)) ((x_id), true);
	return true;
}

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

void GlobalToLocalForPort(GrafPtr g, Point *p)
// allows a quick GlobalToLocal without changing port
{
	Point offset = GET_GLOBAL_COORD_OFFSET(g);
		
	p->h += offset.h;
	p->v += offset.v;
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

// ========== Timer routines

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

