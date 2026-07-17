//==================================================================================
// CrutchSettings.c
// ©2023 Steve Crutchfield
//
// Basic tools for preserving a collection of settings between invocations of code,
// e.g. between an INIT and cdev.
//
// Usage:  presumes it can include a file called 'Settings.h' which looks like the
// following:
	/*
		#define SETTINGS_GESTALT_SELECTOR  	'NLbl'
		#define SETTINGS_MAGIC_NUMBER      	'NLbl'
		#define SETTINGS_RES_TYPE 			'NLbD'
		#define SETTINGS_RES_ID   			-4048
		
		typedef struct {
		// (should be even # bytes so GetHandleSize() check matches)
			OSType magicNumber;
			... // other settings go here
		} Settings;
	*/
//
// Note we assume Gestalt() exists (THINK has glue for it), but we check before 
// calling NewGestalt().
//==================================================================================

#include <SetupA4.h>
#include <GestaltEqu.h>
#include <Traps.h>

// **** next file is specific to each project that uses CrutchSettings.h: ****
#include "Settings.h"

#include "CrutchError.h"
#include "CrutchSettings.h"

// global settings variable
Settings **gSettings;

// function prototype
static pascal OSErr OurSettingsGestalt(OSType gestaltSelector, long *response);

// whoever calls InstallGestaltSelector() (typically an INIT) must call this first 
// in order for globals to work in OurSettingsGestalt!
//
// necessary because including <SetUpA4.h> in each file reserves a separate spot
// (with a DC.L instruction) to save the A0 on entry for later placement into
// A4 when SetupA4 is called.  So we must do this once inside every file that
// calls SetUpA4():
void RememberA0ForSettings(void)
{
	RememberA0();
}

static pascal OSErr OurSettingsGestalt(OSType gestaltSelector, long *response)
// Gestalt selector routine, normally installed by INIT
{
	WITH_A4 (  // (since we are a Toolbox callback -- can't assume A4 is set up)
		*response = (long) gSettings;
	);	

	return noErr;
}

Boolean InstallGestaltSelector(void)
// normally called by INIT:  install our Gestalt selector, which a CDEV can later
// use to get a handle to gSettings
// 
// returns false on error
{
	AssertMesgReturn(!ADDR_IS_IN_APPLZONE((Ptr) OurSettingsGestalt),
					 "trying to install gestalt selector when not running in system heap?",
					 false);

	AssertMesgReturn(TrapAvailable(_NewGestalt),
					 "Gestalt Manager not available",
					 false);

	AssertMesgReturn(noErr == NewGestalt(SETTINGS_GESTALT_SELECTOR, OurSettingsGestalt),
					 "multiple instances of " APP_NAME " might be running?  (couldn't install Gestalt selector)", 
					 false);

	return true;
}

Boolean IsOurGestaltInstalled(void)
{
	long ignore;
	return noErr == Gestalt(SETTINGS_GESTALT_SELECTOR, &ignore);
}

Boolean LoadSettingsFromResource(void)
// load settings resource into gSettings, return false on error
//
// does NOT detach resource -- if running in an INIT, we must do this ourselves
// after calling this function
//
// assumes A4 already set up
{
	Settings **settings = NULL;
	
	gSettings = NULL;

	if (ConfirmResourceWithFlags(
			settings = (Settings **) GetResource(SETTINGS_RES_TYPE, SETTINGS_RES_ID),
			resSysHeap))
	{	
		const Size sz = GetHandleSize((Handle) settings);

		if(    AssertMesg(sz >= 0, 
					"Resource gave us a settings handle that caused an error in GetHandleSize")
			&& AssertMesg(sz > 0, 
					"Resource gave us a zero-length settings handle")
			&& AssertMesg(sz == sizeof(Settings), 
					"Resource gave us a settings handle that's the wrong size")
			&& AssertMesg((**settings).magicNumber == SETTINGS_MAGIC_NUMBER, 
					"Resource gave us settings with bad magic number"))
		{
			gSettings = settings;
		}
	}

	return gSettings != NULL;
}

Boolean LoadSettingsFromGestalt(void)
// normally called by cdev to use the Gestalt selector to get a pointer to
// the INIT's gSettings structure
//
// returns true if looks like the INIT loaded properly at startup, else false
// (in which case we try to load settings from a resource -- if this failed,
// gSettings will be NULL)
//
// assumes A4 already set up
{
	Settings **settings = NULL;
	
	gSettings = NULL;

	if (Gestalt(SETTINGS_GESTALT_SELECTOR, (long *) &settings) == noErr  // Gestalt worked
		&& AssertMesg(settings != NULL, 
				"Gestalt gave us a null settings handle"))
	{
		const Size sz = GetHandleSize((Handle) settings);

		if(    AssertMesg(sz >= 0, 
					"Gestalt gave us a settings handle that caused an error in GetHandleSize")
			&& AssertMesg(sz > 0, 
					"Gestalt gave us a zero-length settings handle")
			&& AssertMesg(sz == sizeof(Settings), 
					"Gestalt gave us a settings handle that's the wrong size")
			&& AssertMesg((**settings).magicNumber == SETTINGS_MAGIC_NUMBER, 
					"Gestalt gave us settings with bad magic number"))
		{
			gSettings = settings;
		}
	}

	return gSettings != NULL;
}

CdevSettingsResult GetCdevSettings(void)
// intended to be used in a cdev to get settings -- first checking to see if 
// Gestalt selector is installed; if not, tries to get them from a resource
//
// Calls Complain() (indirectly) if it gets garbage back from Gestalt or resource,
// but fails silently (and returns an error) if it just can't find anything
//
// assumes A4 already set up
{
	if (LoadSettingsFromGestalt())
		return kGotSettingsFromGestalt;
	else
	{
		// gestalt didn't work, try loading from resource:
		
		if (LoadSettingsFromResource())
		{
			// yes, we got handle to settings resource -- user can work with the cdev
			// but changes won't be effective instantly
			return kGotSettingsFromResource;
		}
		else
		{
			gSettings = NULL;
			return kErrorGettingSettings;

			// cdev should now call 'Error(cdevGenErr)' (cdev class member func) 
			// to tell OS to unload the cdev
		}
	}

	NOT_REACHABLE;
}

void SaveSettings(void)
// normally called by cdev, this writes updated settings struct to a resource
//
// assumes A4 already set up
{
	Handle res;
	
	if (!ConfirmResource(res = GetResource(SETTINGS_RES_TYPE, SETTINGS_RES_ID)))
	{
		Complain("\pcouldn't get settings resource to update it");
		return;
	}
	
	RmveResource(res);	
	AssertMesgReturn(ResError() == noErr, 
		"couldn't remove old settings resource to update it", );

	if (res != (Handle) gSettings)	// (equal when settings were loaded from this very
		DisposeHandle(res);			// resource; else dispose the on-disk copy we loaded)
		
	AddResource((Handle) gSettings, SETTINGS_RES_TYPE, SETTINGS_RES_ID, "\p");
	AssertMesgReturn(ResError() == noErr,
		"couldn't add settings resource", );
	
	SetResAttrs((Handle) gSettings, GetResAttrs((Handle) gSettings) | resSysHeap);
	AssertMesgReturn(ResError() == noErr,
		"couldn't set System Heap bit on updated settings resource", );

	WriteResource((Handle) gSettings);
	AssertMesg(ResError() == noErr,
		"couldn't save settings");

	DetachResource((Handle) gSettings);
}
