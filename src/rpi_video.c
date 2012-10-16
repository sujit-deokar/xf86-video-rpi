#include "config.h"
#include <xorg-server.h>
#include <xf86.h>
#include <xf86_OSproc.h>
#include <micmap.h>
#include <gcstruct.h>
#include <X11/extensions/render.h>
#include "rpi_video.h"
#include <migc.h>
#include <mi.h>

_X_EXPORT DriverRec RPIDriver = {
	VERSION,
	RPI_DRIVER_NAME,
	RPIIdentify,
	RPIProbe,
	RPIAvailableOptions,
	NULL,
	0
};

static SymTabRec RPIChipsets[] = {
	{ 0x0314, "Raspberry Pi (BCM2835)" },
	{ -1, NULL }
};

static XF86ModuleVersionInfo rpiVersRec =
{
	RPI_DRIVER_NAME,
	RPI_MODULEVENDORSTRING,
	MODINFOSTRING1,
	MODINFOSTRING2,
	XORG_VERSION_CURRENT,
	RPI_MAJOR_VERSION, RPI_MINOR_VERSION, RPI_PATCHLEVEL,
	ABI_CLASS_VIDEODRV,
	ABI_VIDEODRV_VERSION,
	MOD_CLASS_VIDEODRV,
	{0,0,0,0}
};

_X_EXPORT XF86ModuleData rpiModuleData = { &rpiVersRec, rpiSetup, NULL };

static const OptionInfoRec RPIOptions[] = {
	{ OPTION_HW_CURSOR, "HWcursor",  OPTV_BOOLEAN, {0}, FALSE },
	{ OPTION_NOACCEL,   "NoAccel",   OPTV_BOOLEAN, {0}, FALSE },
	{ -1,               NULL,        OPTV_NONE,    {0}, FALSE }
};

static void printConfig( ScrnInfoPtr scrn, EGLDisplay disp, EGLConfig config )
{
   EGLint val;
   const char* names[] = { "EGL_BUFFER_SIZE", "EGL_RED_SIZE", "EGL_GREEN_SIZE", "EGL_BLUE_SIZE", "EGL_ALPHA_SIZE", "EGL_CONFIG_CAVEAT", "EGL_CONFIG_ID", "EGL_DEPTH_SIZE", "EGL_LEVEL", "EGL_MAX_PBUFFER_WIDTH", "EGL_MAX_PBUFFER_HEIGHT", "EGL_MAX_PBUFFER_PIXELS", "EGL_NATIVE_RENDERABLE", "EGL_NATIVE_VISUAL_ID", "EGL_NATIVE_VISUAL_TYPE", "EGL_SAMPLE_BUFFERS", "EGL_SAMPLES", "EGL_STENCIL_", "EGL_SURFACE_TYPE", "EGL_TRANSPARENT_TYPE", "EGL_TRANSPARENT_RED", "EGL_TRANSPARENT_GREEN", "EGL_TRANSPARENT_BLUE" };
   const EGLint values[] = { EGL_BUFFER_SIZE, EGL_RED_SIZE, EGL_GREEN_SIZE, EGL_BLUE_SIZE, EGL_ALPHA_SIZE, EGL_CONFIG_CAVEAT, EGL_CONFIG_ID, EGL_DEPTH_SIZE, EGL_LEVEL, EGL_MAX_PBUFFER_WIDTH, EGL_MAX_PBUFFER_HEIGHT, EGL_MAX_PBUFFER_PIXELS, EGL_NATIVE_RENDERABLE, EGL_NATIVE_VISUAL_ID, EGL_NATIVE_VISUAL_TYPE, EGL_SAMPLE_BUFFERS, EGL_SAMPLES, EGL_STENCIL_SIZE, EGL_SURFACE_TYPE, EGL_TRANSPARENT_TYPE, EGL_TRANSPARENT_RED_VALUE, EGL_TRANSPARENT_GREEN_VALUE, EGL_TRANSPARENT_BLUE_VALUE };
   const int nValues = sizeof(values)/sizeof(EGLint);

   ErrorF( "n values = %i\n", nValues );
   for(int i = 0; i < nValues; ++i )
   {
      eglGetConfigAttrib(disp,config,values[i],&val);
      ErrorF("%s = %i\n", names[i], val);
   }
}

static void RPIIdentify(int flags)
{
	xf86PrintChipsets( RPI_NAME, "Driver for Raspberry Pi", RPIChipsets );
}

static Bool RPIProbe(DriverPtr drv, int flags)
{
	int numDevSections, numUsed;
	GDevPtr *devSections;
	int *usedChips;
	int i;
	Bool foundScreen = FALSE;

	if (!xf86LoadDrvSubModule(drv, "fb"))
		return FALSE;

	if( (numDevSections = xf86MatchDevice(RPI_DRIVER_NAME,&devSections)) <= 0 ) return FALSE;

	for( i = 0; i < numDevSections; ++i )
	{
		ScrnInfoPtr sInfo = xf86AllocateScreen(drv, 0);
		int entity = xf86ClaimNoSlot(drv, 0, devSections[i], TRUE);
		xf86AddEntityToScreen(sInfo,entity); 
		sInfo->driverVersion = VERSION;
		sInfo->driverName = RPI_DRIVER_NAME;
		sInfo->name = RPI_NAME;
		sInfo->Probe = RPIProbe;
		sInfo->PreInit = RPIPreInit;
		sInfo->ScreenInit = RPIScreenInit;
		sInfo->SwitchMode = RPISwitchMode;
		sInfo->AdjustFrame = RPIAdjustFrame;
		sInfo->EnterVT = RPIEnterVT;
		sInfo->LeaveVT = RPILeaveVT;
		sInfo->FreeScreen = RPIFreeScreen;
		foundScreen = TRUE;
	}

	free(devSections);
	return foundScreen;
}

static const OptionInfoRec* RPIAvailableOptions(int flags, int bus)
{
	return RPIOptions;
}

static pointer rpiSetup(pointer module, pointer opts, int *errmaj, int *errmin)
{
	static Bool setupDone = FALSE;

	/* This module should be loaded only once, but check to be sure. */

	if (!setupDone) {
		/*
		 * Modules that this driver always requires may be loaded
		 * here  by calling LoadSubModule().
		 */

		setupDone = TRUE;
		xf86AddDriver(&RPIDriver,module,0);
		/*
		 * The return value must be non-NULL on success even though
		 * there is no TearDownProc.
		 */
		return (pointer)1;
	} else {
		if (errmaj) *errmaj = LDR_ONCEONLY;
		return NULL;
	}
}

static Bool RPIGetRec(ScrnInfoPtr pScrn)
{
	if( pScrn->driverPrivate != NULL ) return TRUE;
	pScrn->driverPrivate = xnfcalloc(sizeof(RPIRec), 1);
	if( pScrn->driverPrivate == NULL ) return FALSE;
	return TRUE;
}

static void RPIFreeRec(ScrnInfoPtr pScrn)
{
	if( pScrn->driverPrivate == NULL ) return;
	free(pScrn->driverPrivate);
	pScrn->driverPrivate = NULL;
}

static void RPISave( ScrnInfoPtr pScrn )
{
	ErrorF("RPISave\n");
}

static Bool RPIPreInit(ScrnInfoPtr pScrn,int flags)
{
	ErrorF("Start PreInit\n");

	pScrn->monitor = pScrn->confScreen->monitor;

	RPIGetRec(pScrn);
	
	bcm_host_init();
	EGLConfig config;
	EGLBoolean result;
	EGLint num_config;

	RPIPtr state = RPIPTR(pScrn);
	static const EGLint attribute_list[] =
	{
		EGL_RED_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_BLUE_SIZE, 8,
		EGL_ALPHA_SIZE, 8,
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_NONE
	};

	static const EGLint context_attributes[] = 
	{
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE
	};

	// get an EGL display connection
	state->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	assert(state->display!=EGL_NO_DISPLAY);

	// initialize the EGL display connection
	result = eglInitialize(state->display, NULL, NULL);
	assert(EGL_FALSE != result);

	// get an appropriate EGL frame buffer configuration
	result = eglChooseConfig(state->display, attribute_list, &config, 1, &num_config);
	assert(EGL_FALSE != result);
	printConfig(pScrn,state->display,config);

	// get an appropriate EGL frame buffer configuration
	//   result = eglBindAPI(EGL_OPENGL_ES_API);
	//   assert(EGL_FALSE != result);
	//   check();

	// create an EGL rendering context
	state->context = eglCreateContext(state->display, config, EGL_NO_CONTEXT, context_attributes);
	if( state->context == EGL_NO_CONTEXT )
	{
		ERROR_MSG("No context!");
		goto fail;
	} else
	{
		INFO_MSG("Context get! %p", state->context);
	}
	assert(state->context!=EGL_NO_CONTEXT);

	int bpp;
	eglGetConfigAttrib(state->display,config,EGL_BUFFER_SIZE,&bpp);

	if( !xf86SetDepthBpp(pScrn,0,0,bpp,0) )
	{
		goto fail;
	}
	
	rgb c;
	c.red = 0;
	c.green = 0;
	c.blue = 0;
	if( !xf86SetWeight(pScrn,c,c) )
	{
		goto fail;
	}

	Gamma g;
	g.red = 0;
	g.green = 0;
	g.blue = 0;
	if( !xf86SetGamma(pScrn,g) )
	{
		goto fail;
	}

	// Should be auto-detecting TrueColor, doesn't...
	if( !xf86SetDefaultVisual(pScrn, 0) )
	{
		goto fail;
	}

	pScrn->currentMode = calloc(1,sizeof(DisplayModeRec));
	pScrn->zoomLocked = TRUE;
	graphics_get_display_size(0, &pScrn->currentMode->HDisplay, &pScrn->currentMode->VDisplay );	
	pScrn->modes = xf86ModesAdd(pScrn->modes,pScrn->currentMode);
	//	intel_glamor_pre_init(pScrn);	

	ErrorF("PreInit Success\n");
	return TRUE;

fail:
	ErrorF("PreInit Failed\n");
	RPIFreeRec(pScrn);
	return FALSE;
}

static Bool RPIModeInit(ScrnInfoPtr pScrn, DisplayModePtr pMode)
{
	ErrorF("RPIModeInit\n");
	return TRUE;
}

static Bool RPICreateScreenResources( ScreenPtr pScreen )
{	
	ErrorF("RPICreateScreenResources\n");
	return TRUE;
}

void RPIPutImage( DrawablePtr pDraw, GCPtr pGC, int depth, int x, int y, int w, int h, int leftpad, int format, char* pBits )
{
	ErrorF("RPIPutImage\n");
}

void RPICopyArea( DrawablePtr pSrc, DrawablePtr pDest, GCPtr pGC, int srcx, int srcy, int w, int h, int destx, int desty )
{
	ErrorF("RPICopyArea\n");
}

void RPICopyPlane( DrawablePtr pSrc, DrawablePtr pDest, GCPtr pGC, int srcx, int srcy, int w, int h, int destx, int desty, unsigned long plane )
{
	ErrorF("RPICopyPlane\n");
}

void RPIPolyPoint( DrawablePtr pDraw, GCPtr pGC, int mode, int npt, DDXPointPtr pptInit )
{
	ErrorF("RPIPolyPoint\n");
}

void RPIPolyLines( DrawablePtr pDraw, GCPtr pGC, int mode, int npt, DDXPointPtr pptInit )
{
	ErrorF("RPIPolyPoint\n");
}

void RPIPolySegment( DrawablePtr pDraw, GCPtr pGC, int nSeg, xSegment* pSegs )
{
	ErrorF("RPIPolySegment\n");
}

void RPIPolyRectangle( DrawablePtr pDraw, GCPtr pGC, int nRecs, xRectangle* pRect )
{
	ErrorF("RPIPolyRectangle\n");
}

void RPIPolyArc( DrawablePtr pDraw, GCPtr pGC, int nArcs, xArc* pArcs )
{
	ErrorF("RPIPolyArc\n");
}

void RPIFillPolygon( DrawablePtr pDraw, GCPtr pGC, int shape, int mode, int count, DDXPointPtr pPts )
{
	ErrorF("RPIFillPolygon\n");
}

void RPIPolyFillRect( DrawablePtr pDraw, GCPtr pGC, int nRects, xRectangle* rects)
{
	ErrorF("RPIPolyFillRect\n");
}

void RPIPolyFillArc( DrawablePtr pDraw, GCPtr pGC, int nArcs, xArc* arcs )
{
	ErrorF("RPIPolyFillArc\n");
}

void RPIPolyText8( DrawablePtr pDraw, GCPtr pGC, int x, int y, int count, char* chars )
{
	ErrorF("RPIPolyText8\n");
}

void RPIPolyText16( DrawablePtr pDraw, GCPtr pGC, int x, int y, int count, unsigned short* chars )
{
	ErrorF("RPIPolyText16\n");
}

void RPIImageText8(DrawablePtr pDraw, GCPtr pGC, int x, int y, int count, char * chars )
{
	ErrorF("RPIImageText8\n");
}

void RPIImageText16( DrawablePtr pDraw, GCPtr pGC, int x, int y, int count, unsigned short * chars )
{
	ErrorF("RPIImageText16\n");
}

void RPIImageGlyphBlt( DrawablePtr pDraw, GCPtr pGC, int x, int y, unsigned int nglyph, CharInfoPtr * ppci, pointer pglyphBase )
{
	ErrorF("RPIImageGlyphBlt\n");
}

void RPIPolyGlyphBlt( DrawablePtr pDraw, GCPtr pGC, int x, int y, unsigned int nglyph, CharInfoPtr * ppci, pointer pglyphBase )
{
	ErrorF("RPIPolyGlyphBlt\n");
}

void RPIPushPixels( GCPtr pGC, PixmapPtr pPix, DrawablePtr pDraw, int w, int h, int x, int y )
{
	ErrorF("RPIPushPixels\n");
}

static GCOps RPIGCOps = {
0, //RPIFillSpans,
0, //RPISetSpans,
RPIPutImage,
RPICopyArea,
RPICopyPlane,
RPIPolyPoint,
RPIPolyLines,
RPIPolySegment,
RPIPolyRectangle,
RPIPolyArc,
RPIFillPolygon,
RPIPolyFillRect,
RPIPolyFillArc,
RPIPolyText8,
RPIPolyText16,
RPIImageText8,
RPIImageText16,
RPIImageGlyphBlt,
RPIPolyGlyphBlt,
RPIPushPixels
};

void RPIChangeGC(GCPtr pGC, unsigned long mask)
{
	ErrorF("RPIChangeGC\n");
}

void RPIValidateGC(GCPtr pGC, unsigned long changes, DrawablePtr pDraw )
{
	ErrorF("RPIValidateGC\n");
}
/*
void RPICopyGC()
{
}
*/
void RPIDestroyGC( GCPtr pGC )
{
	ErrorF("RPIDestroyGC\n");
}
/*
void RPIChangeClip()
{
}
*/
void RPIDestroyClip( GCPtr pGC )
{
	ErrorF("RPIDestroyClip\n");
}
/*
void RPICopyClip()
{
}
*/
static GCFuncs RPIGCFuncs = {
	RPIValidateGC,
	RPIChangeGC,
miCopyGC,//	RPICopyGC,
	RPIDestroyGC,
miChangeClip,//	RPIChangeClip,
	RPIDestroyClip,
miCopyClip//	RPICopyClip
};
	
static Bool RPICreateGC( GCPtr pGC )
{
	pGC->ops = &RPIGCOps;
	pGC->funcs = &RPIGCFuncs;
	ScreenPtr pScreen = pGC->pScreen;	
	ErrorF("RPICreateGC\n");
	return TRUE;
}

void RPIQueryBestSize( int class, unsigned short* w, unsigned short* h, ScreenPtr pScreen )
{
	ErrorF("RPIQueryBestSize\n");
}

PixmapPtr RPICreatePixmap( ScreenPtr pScreen, int w, int h, int d, int hint )
{
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum]; 
	ErrorF("RPICreatePixmap\n");
/*
	PixmapPtr p = AllocatePixmap(pScreen,0);
	p->drawable.x = 0;
	p->drawable.y = 0;
	p->drawable.width = w;
	p->drawable.height = h;
	p->drawable.depth = d;
	p->drawable.type = DRAWABLE_PIXMAP;
	p->drawable.class = 0;
	p->drawable.bitsPerPixel = 32;
	p->drawable.id = 0;
	p->refcnt = 1;
	p->devKind = w * 4;
	p->usage_hint = hint;
	p->drawable.serialNumber = NEXT_SERIAL_NUMBER;	
	p->drawable.pScreen = pScreen;
#ifdef COMPOSITE
	p->screen_x = 0;
	p->screen_y = 0;
#endif
	p->devPrivates = 0;
	memset(&p->devPrivates,0,sizeof(DevUnion));
	return p;
*/
	return fbCreatePixmapBpp( pScreen, w, h, d, 32, hint );
}
 
void RPIGetImage( DrawablePtr pDraw, int sx, int sy, int w, int h, unsigned int format, unsigned long planemask, char* pdstLine )
{
	ErrorF("RPIGetImage\n");
}

void RPIDestroyPixmap( PixmapPtr p )
{
	fbDestroyPixmap(p);
	ErrorF("RPIDestroyPixmap\n");
}

Bool RPICreateWindow( WindowPtr pWin )
{
	ErrorF("RPICreateWindow\n");
	return TRUE;
}

void RPIDestroyWindow( WindowPtr pWin )
{
}

void RPIPositionWindow( WindowPtr pWin, int x, int y )
{
	ErrorF("RPIPositionWindow\n");
}

void RPIChangeWindowAttributes( WindowPtr pWin, unsigned long mask )
{
	ErrorF("RPIChangeWindowAttributes\n");
}

Bool RPIDeviceCursorInitialize( DeviceIntPtr pDev, ScreenPtr pScreen )
{
	ErrorF("RPIDeviceCursorInitialize\n");
	return TRUE;
}

void RPIRealizeWindow( WindowPtr pWin )
{
	ErrorF("RPIRealizeWindow\n");
}

void RPIWindowExposures( WindowPtr pWin, RegionPtr region, RegionPtr others )
{
	ErrorF("RPIWindowExposures\n");
}

void RPIRealizeCursor( DeviceIntPtr pDev, ScreenPtr pScreen, CursorPtr pCursor )
{
	ErrorF("RPIRealizeCursor\n");
}

void RPICursorLimits( DeviceIntPtr pDev, ScreenPtr pScreen, CursorPtr pCursor, BoxPtr pHotbox, BoxPtr pTopLeft )
{
	ErrorF("RPICursorLimits\n");
}

void RPIConstrainCursor( DeviceIntPtr pDev, ScreenPtr pScreen, BoxPtr pBox )
{
	ErrorF("RPIConstrainCursor\n");
}

void RPISetCursorPosition( DeviceIntPtr pDev, ScreenPtr pScreen, int x, int y, Bool genEvent )
{
	ErrorF("RPISetCursorPosition\n");
}

void RPIDisplayCursor( DeviceIntPtr pDev, ScreenPtr pScreen, CursorPtr pCursor )
{
	ErrorF("RPIDisplayCursor\n");
}

void RPISaveScreen( ScreenPtr pScreen, int on )
{
	ErrorF("RPISaveScreen\n");
}

void RPIBlockHandler( int sNum, pointer bData, pointer pTimeout, pointer pReadmask )
{
//	ErrorF("RPIBlockHandler\n");
	struct timeval** tvpp = (struct timeval**)pTimeout;
	(*tvpp)->tv_sec = 0;
	(*tvpp)->tv_usec = 100;
}

void RPIWakeupHandler( int sNum, pointer wData, unsigned long result, pointer pReadmask )
{
//	ErrorF("RPIWakeupHandler\n");
}

Bool RPICreateColormap( ColormapPtr pmap )
{
	ErrorF("RPICreateColormap\n");
	return TRUE;
}

void RPIInstallColormap( ColormapPtr pmap )
{
	ErrorF("RPIInstallColormap\n");
}

void RPIResolveColor( short unsigned int* pred, short unsigned int* pgreen, short unsigned int* pblue, VisualPtr pVisual )
{
	ErrorF("RPIResolveColor\n");
}

void RPIDestroyColormap( ColormapPtr pmap )
{
	ErrorF("RPIDestroyColormap\n");
}

void RPICloseScreen( int index, ScreenPtr pScreen )
{
	ErrorF("RPICloseScreen\n");
}

void RPISetScreenPixmap( PixmapPtr pPixmap )
{
	ErrorF("RPISetScreenPixmap\n");
}

PixmapPtr RPIGetScreenPixmap( ScreenPtr pScreen )
{
	ErrorF("RPIGetScreenPixmap\n");
	return pScreen->PixmapPerDepth[0];
}

void RPIMarkWindow( WindowPtr pWin )
{
	ErrorF("RPIMarkWindow\n");
}

int RPIValidateTree( WindowPtr pParent, WindowPtr pChild, VTKind vtk )
{
	ErrorF("RPIValidateTree\n");
	return 0;
}

void RPIHandleExposures( WindowPtr pWin )
{
	ErrorF("RPIHandleExposures\n");
}

void RPISetWindowPixmap( WindowPtr pWin, PixmapPtr pPix )
{
	ErrorF("RPISetWindowPixmap\n");
}

PixmapPtr RPIGetWindowPixmap( WindowPtr pWin )
{
	ErrorF("RPIGetWindowPixmap\n");
	return 0;
}

static Bool RPIScreenInit(int scrnNum, ScreenPtr pScreen, int argc, char** argv )
{
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];

  pScreen->width = pScrn->currentMode->HDisplay;
  pScreen->height = pScrn->currentMode->VDisplay;
  pScreen->x = 0;
  pScreen->y = 0;
  pScreen->mmWidth = pScreen->width;
  pScreen->mmHeight = pScreen->height;
  pScreen->whitePixel = 0xffffffff;
  pScreen->blackPixel = 0x00000000;

	ErrorF("RPIScreenInit\n");
	pScreen->CreateScreenResources = RPICreateScreenResources;
	pScreen->CreateGC = RPICreateGC;
	pScreen->QueryBestSize = RPIQueryBestSize;
	pScreen->CreatePixmap = RPICreatePixmap;
	pScreen->GetImage = RPIGetImage;
	pScreen->DestroyPixmap = RPIDestroyPixmap;
	pScreen->CreateWindow = RPICreateWindow;
	pScreen->PositionWindow = RPIPositionWindow;
	pScreen->DeviceCursorInitialize = RPIDeviceCursorInitialize;
	pScreen->ChangeWindowAttributes = RPIChangeWindowAttributes;
	pScreen->RealizeWindow = RPIRealizeWindow;
	pScreen->WindowExposures = RPIWindowExposures;
	pScreen->RealizeCursor = RPIRealizeCursor;
	pScreen->CursorLimits = RPICursorLimits;
	pScreen->ConstrainCursor = RPIConstrainCursor;
	pScreen->SetCursorPosition = RPISetCursorPosition;
	pScreen->DisplayCursor = RPIDisplayCursor;
	pScreen->SaveScreen = RPISaveScreen;
	pScreen->BlockHandler = RPIBlockHandler;
	pScreen->WakeupHandler = RPIWakeupHandler;
	pScreen->CreateColormap = RPICreateColormap;
	pScreen->InstallColormap = RPIInstallColormap;
	pScreen->ResolveColor = RPIResolveColor;
	pScreen->DestroyColormap = RPIDestroyColormap;
	pScreen->DestroyWindow = RPIDestroyWindow;
	pScreen->CloseScreen = RPICloseScreen;
	pScreen->GetScreenPixmap = RPIGetScreenPixmap;
	pScreen->SetScreenPixmap = RPISetScreenPixmap;
	pScreen->MarkWindow = RPIMarkWindow;
	pScreen->ValidateTree = RPIValidateTree;
	pScreen->HandleExposures = RPIHandleExposures;
	pScreen->SetWindowPixmap = RPISetWindowPixmap;
	pScreen->GetWindowPixmap = RPIGetWindowPixmap;
/*
	pScreen->ClipNotify = RPIClipNotify;
	pScreen->GetSpans = RPIGetSpans;
	pScreen->GetStaticColormap = RPIGetStaticColormap;
	pScreen->ListInstalledColormaps = RPIInstalledColormaps;
	pScreen->PointerNonInterestBox = RPIPointerNonInterestBox;
	pScreen->RealizeFont = RPIRealizeFont;
	pScreen->RecolorCursor = RPIRecolorCursor;
	pScreen->StoreColors = RPIStoreColors;
	pScreen->UninstallColormap = RPIUninstallColormape;
	pScreen->UnrealizeCursor = RPIUnrealizeCursor;
	pScreen->UnrealizeFont = RPIUnrealizeFont;
	pScreen->UnrealizeWindow = RPIUnrealizeWindow;
*/	

	ErrorF("PictureInit\n");
	if( !PictureInit( pScreen, NULL, 0 ) )
	{
		ErrorF("PictureInit failed\n");
		goto fail;
	}
	PictureSetSubpixelOrder(pScreen,SubPixelHorizontalRGB);

	miClearVisualTypes();
	if( !miSetVisualTypes(pScrn->depth,TrueColorMask,pScrn->rgbBits, TrueColor) )
	{
		ErrorF("SetVisualTypes failed\n");
		goto fail;
	}
	if( !miSetPixmapDepths() )
	{
		ErrorF("SetPixmapDepths failed\n");
		goto fail;
	}

	int rootDepth = 0;
	int numVisuals = 0;
	int numDepths = 0;
	VisualID defaultVis;	
	if( !miInitVisuals(&pScreen->visuals,&pScreen->allowedDepths,&numVisuals,&numDepths,&rootDepth,&defaultVis,1<<31,8,-1) )
	{
		ErrorF("InitVisuals failed\n" );
		goto fail;
	}
	pScreen->numVisuals = numVisuals;
	pScreen->numDepths = numDepths;
	pScreen->rootDepth = rootDepth;
	
	for( int i = 0; i < pScreen->numDepths; ++i )
	{
		if( pScreen->allowedDepths[i].depth <= 0 || pScreen->allowedDepths[i].depth > 32 )
		{
			ErrorF("Validating depths failed %i, depth %i\n", i, pScreen->allowedDepths[i].depth );
			goto fail;
		}
	}

	if( !miDCInitialize(pScreen, xf86GetPointerScreenFuncs() ) )
	{
		ErrorF("SpriteInitialize failed\n");
		goto fail;
	}

	xf86DisableRandR();
	xf86SetBackingStore(pScreen);
	miCreateDefColormap(pScreen);

	ErrorF("ScreenInit Success\n");
	return TRUE;
fail:
	ErrorF("ScreeInit Failed\n");
	return FALSE;
}

static Bool RPISwitchMode(int scrnNum, DisplayModePtr pMode, int flags)
{
	ErrorF( "RPISwitchMode\n" );
	return TRUE;
}

static void RPIAdjustFrame(int scrnNum, int x, int y, int flags)
{
	ErrorF( "RPIAdjustFrame\n" );
}

static Bool RPIEnterVT(int scrnNum, int flags)
{
	ErrorF("RPIEnterVT\n");
	return TRUE;
}

static void RPILeaveVT(int scrnNum, int flags)
{
	ErrorF("RPILeaveVT\n" );
}

static void RPIFreeScreen(int scrnNum, int flags)
{
	ErrorF("RPIFreeScreen\n" );
}

