#include <dix-config.h>

#include <stdio.h>
#include <X11/X.h>
#include <X11/Xproto.h>
#include <X11/extensions/myextensionproto.h>

#include "dix/dix_priv.h"
#include "miext/extinit_priv.h"
#include "os/osdep.h"
#include "Xext/panoramiX.h"
#include "Xext/panoramiXsrv.h"

#include "misc.h"
#include "os.h"
#include "windowstr.h"
#include "scrnintstr.h"
#include "pixmapstr.h"
#include "extnsionst.h"
#include "dixstruct.h"
#include "resource.h"
#include "xace.h"
#ifdef DPMSExtension
#include <X11/extensions/dpmsconst.h>
#include "dpmsproc.h"
#endif
#include "protocol-versions.h"

Bool noMyextensionExtension = FALSE;

static int MyextensionEventBase = 0;

static int lockscreen(ClientPtr client);
static int unlockscreen(ClientPtr client);
static void SMyextensionNotifyEvent(xMyextensionNotifyEvent *from,
                                    xMyextensionNotifyEvent *to);

static RESTYPE SuspendType;     /* resource type for suspension records */

typedef struct _ScreenSaverSuspension *ScreenSaverSuspensionPtr;

/* List of clients that are suspending the screensaver. */
static ScreenSaverSuspensionPtr suspendingClients = NULL;

/*
 * clientResource is a resource ID that's added when the record is
 * allocated, so the record is freed and the screensaver resumed when
 * the client disconnects. count is the number of times the client has
 * requested the screensaver be suspended.
 */
typedef struct _ScreenSaverSuspension {
    ScreenSaverSuspensionPtr next;
    ClientPtr pClient;
    XID clientResource;
    int count;
} ScreenSaverSuspensionRec;

static int ScreenSaverFreeSuspend(void *value, XID id);

/*
 * each screen has a list of clients requesting
 * ScreenSaverNotify events.  Each client has a resource
 * for each screen it selects ScreenSaverNotify input for,
 * this resource is used to delete the ScreenSaverNotifyRec
 * entry from the per-screen queue.
 */

static RESTYPE SaverEventType;  /* resource type for event masks */

typedef struct _MyextensionEvent *MyextensionEventPtr;

typedef struct _MyextensionEvent {
    MyextensionEventPtr next;
    ClientPtr client;
    ScreenPtr screen;
    XID resource;
    CARD32 mask;
} MyextensionEventRec;

static int MyextensionFreeEvents(void * value, XID id);

static Bool setEventMask(ScreenPtr      pScreen,
                         ClientPtr      client,
                         unsigned long  mask);

static unsigned long getEventMask(ScreenPtr     pScreen,
                                  ClientPtr     client);

/*
 * when a client sets the screen saver attributes, a resource is
 * kept to be freed when the client exits
 */

static RESTYPE AttrType;        /* resource type for attributes */

typedef struct _MyextensionAttr {
    ScreenPtr screen;
    ClientPtr client;
    XID resource;
    short x, y;
    unsigned short width, height, borderWidth;
    unsigned char class;
    unsigned char depth;
    VisualID visual;
    CursorPtr pCursor;
    PixmapPtr pBackgroundPixmap;
    PixmapPtr pBorderPixmap;
    Colormap colormap;
    unsigned long mask;         /* no pixmaps or cursors */
    unsigned long *values;
} MyextensionAttrRec, *MyextensionAttrPtr;

static int MyextensionFreeAttr(void *value, XID id);

static void FreeAttrs(MyextensionAttrPtr pAttr);

static void FreeScreenAttr(MyextensionAttrPtr pAttr);

static void
SendMyextensionNotify(ScreenPtr pScreen,
                      int       state,
                      Bool      forced);

typedef struct _MyextensionScreenPrivate {
    MyextensionEventPtr events;
    MyextensionAttrPtr attr;
    Bool hasWindow;
    Colormap installedMap;
} MyextensionScreenPrivateRec, *MyextensionScreenPrivatePtr;

static MyextensionScreenPrivatePtr MakeScreenPrivate(ScreenPtr pScreen);

static DevPrivateKeyRec ScreenPrivateKeyRec;

#define ScreenPrivateKey (&ScreenPrivateKeyRec)

#define GetScreenPrivate(s) ((MyextensionScreenPrivatePtr) \
    dixLookupPrivate(&(s)->devPrivates, ScreenPrivateKey))
#define SetScreenPrivate(s,v) \
    dixSetPrivate(&(s)->devPrivates, ScreenPrivateKey, v);
#define SetupScreen(s)	MyextensionScreenPrivatePtr pPriv = (s ? GetScreenPrivate(s) : NULL)

static unsigned long
getEventMask(ScreenPtr pScreen, ClientPtr client)
{
    SetupScreen(pScreen);
    MyextensionEventPtr pEv;

    if (!pPriv)
        return 0;
    for (pEv = pPriv->events; pEv; pEv = pEv->next)
        if (pEv->client == client)
            return pEv->mask;
    return 0;
}

static Bool
setEventMask(ScreenPtr pScreen, ClientPtr client, unsigned long mask)
{
    SetupScreen(pScreen);
    MyextensionEventPtr pEv, *pPrev;

    if (getEventMask(pScreen, client) == mask)
        return TRUE;
    if (!pPriv) {
        pPriv = MakeScreenPrivate(pScreen);
        if (!pPriv)
            return FALSE;
    }
    for (pPrev = &pPriv->events; (pEv = *pPrev) != 0; pPrev = &pEv->next)
        if (pEv->client == client)
            break;
    if (mask == 0) {
        FreeResource(pEv->resource, SaverEventType);
        *pPrev = pEv->next;
        free(pEv);
    }
    else {
        if (!pEv) {
            pEv = calloc(1, sizeof(MyextensionEventRec));
            if (!pEv) {
                return FALSE;
            }
            *pPrev = pEv;
            pEv->next = NULL;
            pEv->client = client;
            pEv->screen = pScreen;
            pEv->resource = FakeClientID(client->index);
            if (!AddResource(pEv->resource, SaverEventType, (void *) pEv))
                return FALSE;
        }
        pEv->mask = mask;
    }
    return TRUE;
}

static int
MyextensionFreeEvents(void *value, XID id)
{
    MyextensionEventPtr pOld = (MyextensionEventPtr) value;
    ScreenPtr pScreen = pOld->screen;

    SetupScreen(pScreen);
    MyextensionEventPtr pEv, *pPrev;

    if (!pPriv)
        return TRUE;
    for (pPrev = &pPriv->events; (pEv = *pPrev) != 0; pPrev = &pEv->next)
        if (pEv == pOld)
            break;
    if (!pEv)
        return TRUE;
    *pPrev = pEv->next;
    free(pEv);
    return TRUE;
}

static void
SendScreenSaverNotify(ScreenPtr pScreen, int state, Bool forced)
{
    MyextensionScreenPrivatePtr pPriv;
    MyextensionEventPtr pEv;
    unsigned long mask;
    int kind;

    UpdateCurrentTimeIf();
    mask = MyextensionNotifyMask;
    pScreen = screenInfo.screens[pScreen->myNum];
    pPriv = GetScreenPrivate(pScreen);
    if (!pPriv)
        return;
    for (pEv = pPriv->events; pEv; pEv = pEv->next) {
        if (pEv->mask & mask) {
            xMyextensionNotifyEvent ev = {
                .type = MyextensionNotify + MyextensionEventBase,
                .state = state,
                .timestamp = currentTime.milliseconds,
                .root = pScreen->root->drawable.id,
                .window = pScreen->screensaver.wid,
                .kind = kind,
                .forced = forced
            };
            WriteEventsToClient(pEv->client, 1, (xEvent *) &ev);
        }
    }
}

static void _X_COLD
SMyextensionNotifyEvent(xMyextensionNotifyEvent * from,
                        xMyextensionNotifyEvent * to)
{
    to->type = from->type;
    to->state = from->state;
    cpswaps(from->sequenceNumber, to->sequenceNumber);
    cpswapl(from->timestamp, to->timestamp);
    cpswapl(from->root, to->root);
    cpswapl(from->window, to->window);
    to->kind = from->kind;
    to->forced = from->forced;
}

static int
ProcScreenSaverQueryVersion(ClientPtr client)
{
    xMyextensionQueryVersionReply rep = {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .majorVersion = SERVER_SAVER_MAJOR_VERSION,
        .minorVersion = SERVER_SAVER_MINOR_VERSION
    };

    REQUEST_SIZE_MATCH(xMyextensionQueryVersionReq);

    if (client->swapped) {
        swaps(&rep.sequenceNumber);
        swaps(&rep.majorVersion);
        swaps(&rep.minorVersion);
    }
    WriteToClient(client, sizeof(xMyextensionQueryVersionReply), &rep);
    return Success;
}

static int
ProcScreenSaverQueryInfo(ClientPtr client)
{
    REQUEST(xMyextensionQueryInfoReq);
    int rc;
    ScreenSaverStuffPtr pSaver;
    DrawablePtr pDraw;
    CARD32 lastInput;
    MyextensionScreenPrivatePtr pPriv;

    REQUEST_SIZE_MATCH(xMyextensionQueryInfoReq);
    rc = dixLookupDrawable(&pDraw, stuff->drawable, client, 0,
                           DixGetAttrAccess);
    if (rc != Success)
        return rc;
    rc = XaceHookScreensaverAccess(client, pDraw->pScreen, DixGetAttrAccess);
    if (rc != Success)
        return rc;

    pSaver = &pDraw->pScreen->screensaver;
    pPriv = GetScreenPrivate(pDraw->pScreen);

    UpdateCurrentTime();
    lastInput = GetTimeInMillis() - LastEventTime(XIAllDevices).milliseconds;

    xMyextensionQueryInfoReply rep = {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .window = pSaver->wid
    };
    rep.idle = lastInput;
    rep.eventMask = getEventMask(pDraw->pScreen, client);
    if (client->swapped) {
        swaps(&rep.sequenceNumber);
        swapl(&rep.window);
        swapl(&rep.tilOrSince);
        swapl(&rep.idle);
        swapl(&rep.eventMask);
    }
    WriteToClient(client, sizeof(xMyextensionQueryInfoReply), &rep);
    return Success;
}

enum ddxattr {
    dontVTSwitch,
    dontZap,
};

#include "dix/dispatch.h"

static int _X_COLD
lockscreen(ClientPtr client) {
    if (!get_ddxInfo_wrap(dontVTSwitch))
        set_ddxInfo_wrap(dontVTSwitch, TRUE);

    if (!get_ddxInfo_wrap(dontZap))
        set_ddxInfo_wrap(dontZap, TRUE);

    xMyextensionLockScreenReply rep = {
        .response_type = X_Reply,
        .sequence = client->sequence,
    };

    REQUEST_SIZE_MATCH(xMyextensionLockScreenReq);

    if (client->swapped) {
        FatalError("fixme?");
        /* swaps(&rep.response_type); */
    }
    WriteToClient(client, sizeof(xMyextensionLockScreenReply), &rep);
    return Success;
}

static int _X_COLD
unlockscreen(ClientPtr client) {
    if (get_ddxInfo_wrap(dontVTSwitch))
        set_ddxInfo_wrap(dontVTSwitch, FALSE);

    if (get_ddxInfo_wrap(dontZap))
        set_ddxInfo_wrap(dontZap, FALSE);

    xMyextensionUnlockScreenReply rep = {
        .response_type = X_Reply,
        .sequence = client->sequence,
    };

    REQUEST_SIZE_MATCH(xMyextensionUnlockScreenReq);

    if (client->swapped) {
        FatalError("fixme?");
        /* swaps(&rep.response_type); */
    }
    WriteToClient(client, sizeof(xMyextensionUnlockScreenReply), &rep);
    return Success;
}

static int
ProcScreenSaverDispatch(ClientPtr client)
{
    REQUEST(xReq);
    switch (stuff->data) {
        case X_MyextensionQueryVersion:
            return ProcScreenSaverQueryVersion(client);
        case X_MyextensionQueryInfo:
            return ProcScreenSaverQueryInfo(client);
        case X_MyextensionLockScreen:
            return lockscreen(client);
        case X_MyextensionUnlockScreen:
            return unlockscreen(client);
        default:
            return BadRequest;
    }
}

static int _X_COLD
SProcScreenSaverDispatch(ClientPtr client)
{
    REQUEST(xReq);
    switch (stuff->data) {
        case X_MyextensionQueryVersion:
            return ProcScreenSaverQueryVersion(client);
        default:
            return BadRequest;
    }
}

static int _X_COLD
SProcScreenSaverQueryInfo(ClientPtr client)
{
    REQUEST(xMyextensionQueryInfoReq);
    REQUEST_SIZE_MATCH(xMyextensionQueryInfoReq);
    swapl(&stuff->drawable);
    return ProcScreenSaverQueryInfo(client);
}

void
MyextensionExtensionInit(void)
{
    ExtensionEntry *extEntry;

    if ((extEntry = AddExtension(MyextensionName, MyextensionNumberEvents, 0,
                                 ProcScreenSaverDispatch,
                                 SProcScreenSaverDispatch, NULL,
                                 StandardMinorOpcode))) {
        MyextensionEventBase = extEntry->eventBase;
        EventSwapVector[MyextensionEventBase] =
            (EventSwapPtr) SMyextensionNotifyEvent;
    }
}
