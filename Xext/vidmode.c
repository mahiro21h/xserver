/*

Copyright 1995  Kaleb S. KEITHLEY

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL Kaleb S. KEITHLEY BE LIABLE FOR ANY CLAIM, DAMAGES
OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of Kaleb S. KEITHLEY
shall not be used in advertising or otherwise to promote the sale, use
or other dealings in this Software without prior written authorization
from Kaleb S. KEITHLEY

*/
/* THIS IS NOT AN X CONSORTIUM STANDARD OR AN X PROJECT TEAM SPECIFICATION */

#include <dix-config.h>

#ifdef XF86VIDMODE

#include <X11/X.h>
#include <X11/Xproto.h>
#include <X11/extensions/xf86vmproto.h>

#include "dix/request_priv.h"
#include "os/log_priv.h"

#include "misc.h"
#include "dixstruct.h"
#include "extnsionst.h"
#include "scrnintstr.h"
#include "servermd.h"
#include "vidmodestr.h"
#include "globals.h"
#include "protocol-versions.h"

static int VidModeErrorBase;
static int VidModeAllowNonLocal;

static DevPrivateKeyRec VidModeClientPrivateKeyRec;
#define VidModeClientPrivateKey (&VidModeClientPrivateKeyRec)

static DevPrivateKeyRec VidModePrivateKeyRec;
#define VidModePrivateKey (&VidModePrivateKeyRec)

/* This holds the client's version information */
typedef struct {
    int major;
    int minor;
} VidModePrivRec, *VidModePrivPtr;

#define VM_GETPRIV(c) ((VidModePrivPtr) \
    dixLookupPrivate(&(c)->devPrivates, VidModeClientPrivateKey))
#define VM_SETPRIV(c,p) \
    dixSetPrivate(&(c)->devPrivates, VidModeClientPrivateKey, p)

#ifdef DEBUG
#define DEBUG_P(x) DebugF(x"\n")
#else
#define DEBUG_P(x) /**/
#endif

static DisplayModePtr
VidModeCreateMode(void)
{
    DisplayModePtr mode = calloc(1, sizeof(DisplayModeRec));
    if (mode != NULL) {
        mode->name = "";
        mode->VScan = 1;        /* divides refresh rate. default = 1 */
        mode->Private = NULL;
        mode->next = mode;
        mode->prev = mode;
    }
    return mode;
}

static void
VidModeCopyMode(DisplayModePtr modefrom, DisplayModePtr modeto)
{
    memcpy(modeto, modefrom, sizeof(DisplayModeRec));
}

static int
VidModeGetModeValue(DisplayModePtr mode, int valtyp)
{
    int ret = 0;

    switch (valtyp) {
    case VIDMODE_H_DISPLAY:
        ret = mode->HDisplay;
        break;
    case VIDMODE_H_SYNCSTART:
        ret = mode->HSyncStart;
        break;
    case VIDMODE_H_SYNCEND:
        ret = mode->HSyncEnd;
        break;
    case VIDMODE_H_TOTAL:
        ret = mode->HTotal;
        break;
    case VIDMODE_H_SKEW:
        ret = mode->HSkew;
        break;
    case VIDMODE_V_DISPLAY:
        ret = mode->VDisplay;
        break;
    case VIDMODE_V_SYNCSTART:
        ret = mode->VSyncStart;
        break;
    case VIDMODE_V_SYNCEND:
        ret = mode->VSyncEnd;
        break;
    case VIDMODE_V_TOTAL:
        ret = mode->VTotal;
        break;
    case VIDMODE_FLAGS:
        ret = mode->Flags;
        break;
    case VIDMODE_CLOCK:
        ret = mode->Clock;
        break;
    }
    return ret;
}

static void
VidModeSetModeValue(DisplayModePtr mode, int valtyp, int val)
{
    switch (valtyp) {
    case VIDMODE_H_DISPLAY:
        mode->HDisplay = val;
        break;
    case VIDMODE_H_SYNCSTART:
        mode->HSyncStart = val;
        break;
    case VIDMODE_H_SYNCEND:
        mode->HSyncEnd = val;
        break;
    case VIDMODE_H_TOTAL:
        mode->HTotal = val;
        break;
    case VIDMODE_H_SKEW:
        mode->HSkew = val;
        break;
    case VIDMODE_V_DISPLAY:
        mode->VDisplay = val;
        break;
    case VIDMODE_V_SYNCSTART:
        mode->VSyncStart = val;
        break;
    case VIDMODE_V_SYNCEND:
        mode->VSyncEnd = val;
        break;
    case VIDMODE_V_TOTAL:
        mode->VTotal = val;
        break;
    case VIDMODE_FLAGS:
        mode->Flags = val;
        break;
    case VIDMODE_CLOCK:
        mode->Clock = val;
        break;
    }
    return;
}

static int
ClientMajorVersion(ClientPtr client)
{
    VidModePrivPtr pPriv;

    pPriv = VM_GETPRIV(client);
    if (!pPriv)
        return 0;
    else
        return pPriv->major;
}

static int
ProcVidModeQueryVersion(ClientPtr client)
{
    DEBUG_P("XF86VidModeQueryVersion");
    REQUEST_HEAD_STRUCT(xXF86VidModeQueryVersionReq);

    xXF86VidModeQueryVersionReply rep = {
        .majorVersion = SERVER_XF86VIDMODE_MAJOR_VERSION,
        .minorVersion = SERVER_XF86VIDMODE_MINOR_VERSION
    };

    REPLY_FIELD_CARD16(majorVersion);
    REPLY_FIELD_CARD16(minorVersion);
    REPLY_SEND();
    return Success;
}

static int
oldVidModeGetModeLineReply(ClientPtr client, xXF86VidModeGetModeLineReply *r)
{
    xXF86OldVidModeGetModeLineReply rep = {
        .length = bytes_to_int32(sizeof(xXF86OldVidModeGetModeLineReply) -
                                 sizeof(xGenericReply)),
        .dotclock = r->dotclock,
        .hdisplay = r->hdisplay,
        .hsyncstart = r->hsyncstart,
        .hsyncend = r->hsyncend,
        .htotal = r->htotal,
        .vdisplay = r->vdisplay,
        .vsyncstart = r->vsyncstart,
        .vsyncend = r->vsyncend,
        .vtotal = r->vtotal,
        .flags = r->flags,
        .privsize = r->privsize
    };

    /* the fields already had been swapped by caller (except for length, which
       will be swapped by REQUEST_SEND() */
    REPLY_SEND();
    return Success;
}

static int
ProcVidModeGetModeLine(ClientPtr client)
{
    REQUEST_HEAD_STRUCT(xXF86VidModeGetModeLineReq);
    REQUEST_FIELD_CARD16(screen);

    ScreenPtr pScreen;
    VidModePtr pVidMode;
    DisplayModePtr mode;
    int dotClock;
    int ver;

    DEBUG_P("XF86VidModeGetModeline");

    ver = ClientMajorVersion(client);
    REQUEST_SIZE_MATCH(xXF86VidModeGetModeLineReq);

    if (stuff->screen >= screenInfo.numScreens)
        return BadValue;
    pScreen = screenInfo.screens[stuff->screen];
    pVidMode = VidModeGetPtr(pScreen);
    if (pVidMode == NULL)
        return BadImplementation;

    if (!pVidMode->GetCurrentModeline(pScreen, &mode, &dotClock))
        return BadValue;

    xXF86VidModeGetModeLineReply rep = {
        .dotclock = dotClock,
        .hdisplay = VidModeGetModeValue(mode, VIDMODE_H_DISPLAY),
        .hsyncstart = VidModeGetModeValue(mode, VIDMODE_H_SYNCSTART),
        .hsyncend = VidModeGetModeValue(mode, VIDMODE_H_SYNCEND),
        .htotal = VidModeGetModeValue(mode, VIDMODE_H_TOTAL),
        .hskew = VidModeGetModeValue(mode, VIDMODE_H_SKEW),
        .vdisplay = VidModeGetModeValue(mode, VIDMODE_V_DISPLAY),
        .vsyncstart = VidModeGetModeValue(mode, VIDMODE_V_SYNCSTART),
        .vsyncend = VidModeGetModeValue(mode, VIDMODE_V_SYNCEND),
        .vtotal = VidModeGetModeValue(mode, VIDMODE_V_TOTAL),
        .flags = VidModeGetModeValue(mode, VIDMODE_FLAGS),
        .length = bytes_to_int32(sizeof(xXF86VidModeGetModeLineReply) -
                                    sizeof(xGenericReply)),
        /*
         * Older servers sometimes had server privates that the VidMode
         * extension made available. So to be compatible pretend that
         * there are no server privates to pass to the client.
         */
        .privsize = 0,
    };

    DebugF("GetModeLine - scrn: %d clock: %ld\n",
           stuff->screen, (unsigned long) rep.dotclock);
    DebugF("GetModeLine - hdsp: %d hbeg: %d hend: %d httl: %d\n",
           rep.hdisplay, rep.hsyncstart, rep.hsyncend, rep.htotal);
    DebugF("              vdsp: %d vbeg: %d vend: %d vttl: %d flags: %ld\n",
           rep.vdisplay, rep.vsyncstart, rep.vsyncend,
           rep.vtotal, (unsigned long) rep.flags);

    REPLY_FIELD_CARD32(dotclock);
    REPLY_FIELD_CARD16(hdisplay);
    REPLY_FIELD_CARD16(hsyncstart);
    REPLY_FIELD_CARD16(hsyncend);
    REPLY_FIELD_CARD16(htotal);
    REPLY_FIELD_CARD16(hskew);
    REPLY_FIELD_CARD16(vdisplay);
    REPLY_FIELD_CARD16(vsyncstart);
    REPLY_FIELD_CARD16(vsyncend);
    REPLY_FIELD_CARD16(vtotal);
    REPLY_FIELD_CARD32(flags);
    REPLY_FIELD_CARD32(privsize);

    if (ver < 2)
        return oldVidModeGetModeLineReply(client, &rep);

    REPLY_SEND();
    return Success;
}

static char *fillModeInfoV1(ClientPtr client, char *buf, int dotClock, DisplayModePtr mode)
{
    xXF86OldVidModeModeInfo rep = {
        .dotclock = dotClock,
        .hdisplay = VidModeGetModeValue(mode, VIDMODE_H_DISPLAY),
        .hsyncstart = VidModeGetModeValue(mode, VIDMODE_H_SYNCSTART),
        .hsyncend = VidModeGetModeValue(mode, VIDMODE_H_SYNCEND),
        .htotal = VidModeGetModeValue(mode, VIDMODE_H_TOTAL),
        .vdisplay = VidModeGetModeValue(mode, VIDMODE_V_DISPLAY),
        .vsyncstart = VidModeGetModeValue(mode, VIDMODE_V_SYNCSTART),
        .vsyncend = VidModeGetModeValue(mode, VIDMODE_V_SYNCEND),
        .vtotal = VidModeGetModeValue(mode, VIDMODE_V_TOTAL),
        .flags = VidModeGetModeValue(mode, VIDMODE_FLAGS),
    };

    REPLY_FIELD_CARD32(dotclock);
    REPLY_FIELD_CARD16(hdisplay);
    REPLY_FIELD_CARD16(hsyncstart);
    REPLY_FIELD_CARD16(hsyncend);
    REPLY_FIELD_CARD16(htotal);
    REPLY_FIELD_CARD16(vdisplay);
    REPLY_FIELD_CARD16(vsyncstart);
    REPLY_FIELD_CARD16(vsyncend);
    REPLY_FIELD_CARD16(vtotal);
    REPLY_FIELD_CARD32(flags);

    memcpy(buf, &rep, sizeof(rep));
    return buf + sizeof(rep);
}

static char *fillModeInfoV2(ClientPtr client, char *buf, int dotClock, DisplayModePtr mode)
{
    xXF86VidModeModeInfo rep = {
        .dotclock = dotClock,
        .hdisplay = VidModeGetModeValue(mode, VIDMODE_H_DISPLAY),
        .hsyncstart = VidModeGetModeValue(mode, VIDMODE_H_SYNCSTART),
        .hsyncend = VidModeGetModeValue(mode, VIDMODE_H_SYNCEND),
        .htotal = VidModeGetModeValue(mode, VIDMODE_H_TOTAL),
        .hskew = VidModeGetModeValue(mode, VIDMODE_H_SKEW),
        .vdisplay = VidModeGetModeValue(mode, VIDMODE_V_DISPLAY),
        .vsyncstart = VidModeGetModeValue(mode, VIDMODE_V_SYNCSTART),
        .vsyncend = VidModeGetModeValue(mode, VIDMODE_V_SYNCEND),
        .vtotal = VidModeGetModeValue(mode, VIDMODE_V_TOTAL),
        .flags = VidModeGetModeValue(mode, VIDMODE_FLAGS),
    };

    REPLY_FIELD_CARD32(dotclock);
    REPLY_FIELD_CARD16(hdisplay);
    REPLY_FIELD_CARD16(hsyncstart);
    REPLY_FIELD_CARD16(hsyncend);
    REPLY_FIELD_CARD16(htotal);
    REPLY_FIELD_CARD32(hskew);
    REPLY_FIELD_CARD16(vdisplay);
    REPLY_FIELD_CARD16(vsyncstart);
    REPLY_FIELD_CARD16(vsyncend);
    REPLY_FIELD_CARD16(vtotal);
    REPLY_FIELD_CARD32(flags);

    memcpy(buf, &rep, sizeof(rep));
    return buf + sizeof(rep);
}

static int
ProcVidModeGetAllModeLines(ClientPtr client)
{
    REQUEST_HEAD_STRUCT(xXF86VidModeGetAllModeLinesReq);
    REQUEST_FIELD_CARD16(screen);

    ScreenPtr pScreen;
    VidModePtr pVidMode;
    DisplayModePtr mode;
    int modecount, dotClock;
    int ver;

    DEBUG_P("XF86VidModeGetAllModelines");

    if (stuff->screen >= screenInfo.numScreens)
        return BadValue;
    pScreen = screenInfo.screens[stuff->screen];
    ver = ClientMajorVersion(client);
    pVidMode = VidModeGetPtr(pScreen);
    if (pVidMode == NULL)
        return BadImplementation;

    modecount = pVidMode->GetNumOfModes(pScreen);
    if (modecount < 1)
        return VidModeErrorBase + XF86VidModeExtensionDisabled;

    if (!pVidMode->GetFirstModeline(pScreen, &mode, &dotClock))
        return BadValue;

    int payload_len = modecount * ((ver < 2) ? sizeof(xXF86OldVidModeModeInfo)
                                             : sizeof(xXF86VidModeModeInfo));

    xXF86VidModeGetAllModeLinesReply rep = {
        .modecount = modecount
    };

    char *payload = calloc(1, payload_len);
    if (!payload)
        return BadAlloc;

    char *walk = payload;

    do {
        walk = (ver < 2) ? fillModeInfoV1(client, walk, dotClock, mode)
                         : fillModeInfoV2(client, walk, dotClock, mode);
    } while (pVidMode->GetNextModeline(pScreen, &mode, &dotClock));

    REPLY_SEND_EXTRA(payload, payload_len);
    free(payload);

    return Success;
}

#define MODEMATCH(mode,stuff)	  \
     (VidModeGetModeValue(mode, VIDMODE_H_DISPLAY)  == stuff->hdisplay \
     && VidModeGetModeValue(mode, VIDMODE_H_SYNCSTART)  == stuff->hsyncstart \
     && VidModeGetModeValue(mode, VIDMODE_H_SYNCEND)  == stuff->hsyncend \
     && VidModeGetModeValue(mode, VIDMODE_H_TOTAL)  == stuff->htotal \
     && VidModeGetModeValue(mode, VIDMODE_V_DISPLAY)  == stuff->vdisplay \
     && VidModeGetModeValue(mode, VIDMODE_V_SYNCSTART)  == stuff->vsyncstart \
     && VidModeGetModeValue(mode, VIDMODE_V_SYNCEND)  == stuff->vsyncend \
     && VidModeGetModeValue(mode, VIDMODE_V_TOTAL)  == stuff->vtotal \
     && VidModeGetModeValue(mode, VIDMODE_FLAGS)  == stuff->flags )

static int VidModeAddModeLine(ClientPtr client, xXF86VidModeAddModeLineReq* stuff);

static int
ProcVidModeAddModeLine(ClientPtr client)
{
    int len;

    /* limited to local-only connections */
    if (!VidModeAllowNonLocal && !client->local)
        return VidModeErrorBase + XF86VidModeClientNotLocal;

    DEBUG_P("XF86VidModeAddModeline");

    if (ClientMajorVersion(client) < 2) {
        REQUEST_HEAD_AT_LEAST(xXF86OldVidModeAddModeLineReq);
        len =
            client->req_len -
            bytes_to_int32(sizeof(xXF86OldVidModeAddModeLineReq));

        REQUEST_FIELD_CARD32(screen);
        REQUEST_FIELD_CARD16(hdisplay);
        REQUEST_FIELD_CARD16(hsyncstart);
        REQUEST_FIELD_CARD16(hsyncend);
        REQUEST_FIELD_CARD16(htotal);
        REQUEST_FIELD_CARD16(vdisplay);
        REQUEST_FIELD_CARD16(vsyncstart);
        REQUEST_FIELD_CARD16(vsyncend);
        REQUEST_FIELD_CARD16(vtotal);
        REQUEST_FIELD_CARD32(flags);
        REQUEST_FIELD_CARD32(privsize);
        REQUEST_REST_CARD32();

        if (len != stuff->privsize)
            return BadLength;

        xXF86VidModeAddModeLineReq newstuff = {
            .length = client->req_len,
            .screen = stuff->screen,
            .dotclock = stuff->dotclock,
            .hdisplay = stuff->hdisplay,
            .hsyncstart = stuff->hsyncstart,
            .hsyncend = stuff->hsyncend,
            .htotal = stuff->htotal,
            .hskew = 0,
            .vdisplay = stuff->vdisplay,
            .vsyncstart = stuff->vsyncstart,
            .vsyncend = stuff->vsyncend,
            .vtotal = stuff->vtotal,
            .flags = stuff->flags,
            .privsize = stuff->privsize,
            .after_dotclock = stuff->after_dotclock,
            .after_hdisplay = stuff->after_hdisplay,
            .after_hsyncstart = stuff->after_hsyncstart,
            .after_hsyncend = stuff->after_hsyncend,
            .after_htotal = stuff->after_htotal,
            .after_hskew = 0,
            .after_vdisplay = stuff->after_vdisplay,
            .after_vsyncstart = stuff->after_vsyncstart,
            .after_vsyncend = stuff->after_vsyncend,
            .after_vtotal = stuff->after_vtotal,
            .after_flags = stuff->after_flags,
        };
        return VidModeAddModeLine(client, &newstuff);
    }
    else {
        REQUEST_HEAD_AT_LEAST(xXF86VidModeAddModeLineReq);
        len =
            client->req_len -
            bytes_to_int32(sizeof(xXF86VidModeAddModeLineReq));

        REQUEST_FIELD_CARD16(length);
        REQUEST_FIELD_CARD32(screen);
        REQUEST_FIELD_CARD16(hdisplay);
        REQUEST_FIELD_CARD16(hsyncstart);
        REQUEST_FIELD_CARD16(hsyncend);
        REQUEST_FIELD_CARD16(htotal);
        REQUEST_FIELD_CARD16(hskew);
        REQUEST_FIELD_CARD16(vdisplay);
        REQUEST_FIELD_CARD16(vsyncstart);
        REQUEST_FIELD_CARD16(vsyncend);
        REQUEST_FIELD_CARD16(vtotal);
        REQUEST_FIELD_CARD32(flags);
        REQUEST_FIELD_CARD32(privsize);
        REQUEST_REST_CARD32();

        if (len != stuff->privsize)
            return BadLength;

        return VidModeAddModeLine(client, stuff);
    }
}

static int VidModeAddModeLine(ClientPtr client, xXF86VidModeAddModeLineReq* stuff)
{
    ScreenPtr pScreen;
    DisplayModePtr mode;
    VidModePtr pVidMode;
    int dotClock;

    DebugF("AddModeLine - scrn: %d clock: %ld\n",
           (int) stuff->screen, (unsigned long) stuff->dotclock);
    DebugF("AddModeLine - hdsp: %d hbeg: %d hend: %d httl: %d\n",
           stuff->hdisplay, stuff->hsyncstart,
           stuff->hsyncend, stuff->htotal);
    DebugF("              vdsp: %d vbeg: %d vend: %d vttl: %d flags: %ld\n",
           stuff->vdisplay, stuff->vsyncstart, stuff->vsyncend,
           stuff->vtotal, (unsigned long) stuff->flags);
    DebugF("      after - scrn: %d clock: %ld\n",
           (int) stuff->screen, (unsigned long) stuff->after_dotclock);
    DebugF("              hdsp: %d hbeg: %d hend: %d httl: %d\n",
           stuff->after_hdisplay, stuff->after_hsyncstart,
           stuff->after_hsyncend, stuff->after_htotal);
    DebugF("              vdsp: %d vbeg: %d vend: %d vttl: %d flags: %ld\n",
           stuff->after_vdisplay, stuff->after_vsyncstart,
           stuff->after_vsyncend, stuff->after_vtotal,
           (unsigned long) stuff->after_flags);

    if (stuff->screen >= screenInfo.numScreens)
        return BadValue;
    pScreen = screenInfo.screens[stuff->screen];

    if (stuff->hsyncstart < stuff->hdisplay ||
        stuff->hsyncend < stuff->hsyncstart ||
        stuff->htotal < stuff->hsyncend ||
        stuff->vsyncstart < stuff->vdisplay ||
        stuff->vsyncend < stuff->vsyncstart || stuff->vtotal < stuff->vsyncend)
        return BadValue;

    if (stuff->after_hsyncstart < stuff->after_hdisplay ||
        stuff->after_hsyncend < stuff->after_hsyncstart ||
        stuff->after_htotal < stuff->after_hsyncend ||
        stuff->after_vsyncstart < stuff->after_vdisplay ||
        stuff->after_vsyncend < stuff->after_vsyncstart ||
        stuff->after_vtotal < stuff->after_vsyncend)
        return BadValue;

    pVidMode = VidModeGetPtr(pScreen);
    if (pVidMode == NULL)
        return BadImplementation;

    if (stuff->after_htotal != 0 || stuff->after_vtotal != 0) {
        Bool found = FALSE;

        if (pVidMode->GetFirstModeline(pScreen, &mode, &dotClock)) {
            do {
                if ((pVidMode->GetDotClock(pScreen, stuff->dotclock)
                     == dotClock) && MODEMATCH(mode, stuff)) {
                    found = TRUE;
                    break;
                }
            } while (pVidMode->GetNextModeline(pScreen, &mode, &dotClock));
        }
        if (!found)
            return BadValue;
    }

    mode = VidModeCreateMode();
    if (mode == NULL)
        return BadValue;

    VidModeSetModeValue(mode, VIDMODE_CLOCK, stuff->dotclock);
    VidModeSetModeValue(mode, VIDMODE_H_DISPLAY, stuff->hdisplay);
    VidModeSetModeValue(mode, VIDMODE_H_SYNCSTART, stuff->hsyncstart);
    VidModeSetModeValue(mode, VIDMODE_H_SYNCEND, stuff->hsyncend);
    VidModeSetModeValue(mode, VIDMODE_H_TOTAL, stuff->htotal);
    VidModeSetModeValue(mode, VIDMODE_H_SKEW, stuff->hskew);
    VidModeSetModeValue(mode, VIDMODE_V_DISPLAY, stuff->vdisplay);
    VidModeSetModeValue(mode, VIDMODE_V_SYNCSTART, stuff->vsyncstart);
    VidModeSetModeValue(mode, VIDMODE_V_SYNCEND, stuff->vsyncend);
    VidModeSetModeValue(mode, VIDMODE_V_TOTAL, stuff->vtotal);
    VidModeSetModeValue(mode, VIDMODE_FLAGS, stuff->flags);

    if (stuff->privsize)
        DebugF("AddModeLine - Privates in request have been ignored\n");

    /* Check that the mode is consistent with the monitor specs */
    switch (pVidMode->CheckModeForMonitor(pScreen, mode)) {
    case MODE_OK:
        break;
    case MODE_HSYNC:
    case MODE_H_ILLEGAL:
        free(mode);
        return VidModeErrorBase + XF86VidModeBadHTimings;
    case MODE_VSYNC:
    case MODE_V_ILLEGAL:
        free(mode);
        return VidModeErrorBase + XF86VidModeBadVTimings;
    default:
        free(mode);
        return VidModeErrorBase + XF86VidModeModeUnsuitable;
    }

    /* Check that the driver is happy with the mode */
    if (pVidMode->CheckModeForDriver(pScreen, mode) != MODE_OK) {
        free(mode);
        return VidModeErrorBase + XF86VidModeModeUnsuitable;
    }

    pVidMode->SetCrtcForMode(pScreen, mode);

    pVidMode->AddModeline(pScreen, mode);

    DebugF("AddModeLine - Succeeded\n");

    return Success;
}

static int
VidModeDeleteModeLine(ClientPtr client, xXF86VidModeDeleteModeLineReq* stuff);

static int
ProcVidModeDeleteModeLine(ClientPtr client)
{
    int len;

    /* limited to local-only connections */
    if (!VidModeAllowNonLocal && !client->local)
        return VidModeErrorBase + XF86VidModeClientNotLocal;

    DEBUG_P("XF86VidModeDeleteModeline");

    if (ClientMajorVersion(client) < 2) {
        REQUEST_HEAD_AT_LEAST(xXF86OldVidModeDeleteModeLineReq);
        REQUEST_FIELD_CARD32(screen);
        REQUEST_FIELD_CARD16(hdisplay);
        REQUEST_FIELD_CARD16(hsyncstart);
        REQUEST_FIELD_CARD16(hsyncend);
        REQUEST_FIELD_CARD16(htotal);
        REQUEST_FIELD_CARD16(vdisplay);
        REQUEST_FIELD_CARD16(vsyncstart);
        REQUEST_FIELD_CARD16(vsyncend);
        REQUEST_FIELD_CARD16(vtotal);
        REQUEST_FIELD_CARD32(flags);
        REQUEST_FIELD_CARD32(privsize);
        REQUEST_REST_CARD32();

        len =
            client->req_len -
            bytes_to_int32(sizeof(xXF86OldVidModeDeleteModeLineReq));
        if (len != stuff->privsize) {
            DebugF("req_len = %ld, sizeof(Req) = %d, privsize = %ld, "
                   "len = %d, length = %d\n",
                   (unsigned long) client->req_len,
                   (int) sizeof(xXF86VidModeDeleteModeLineReq) >> 2,
                   (unsigned long) stuff->privsize, len, client->req_len);
            return BadLength;
        }

        /* convert from old format */
        xXF86VidModeDeleteModeLineReq newstuff = {
            .length = client->req_len,
            .screen = stuff->screen,
            .dotclock = stuff->dotclock,
            .hdisplay = stuff->hdisplay,
            .hsyncstart = stuff->hsyncstart,
            .hsyncend = stuff->hsyncend,
            .htotal = stuff->htotal,
            .hskew = 0,
            .vdisplay = stuff->vdisplay,
            .vsyncstart = stuff->vsyncstart,
            .vsyncend = stuff->vsyncend,
            .vtotal = stuff->vtotal,
            .flags = stuff->flags,
            .privsize = stuff->privsize,
        };
        return VidModeDeleteModeLine(client, &newstuff);
    }
    else {
        REQUEST_HEAD_AT_LEAST(xXF86VidModeDeleteModeLineReq);
        REQUEST_FIELD_CARD32(screen);
        REQUEST_FIELD_CARD16(hdisplay);
        REQUEST_FIELD_CARD16(hsyncstart);
        REQUEST_FIELD_CARD16(hsyncend);
        REQUEST_FIELD_CARD16(htotal);
        REQUEST_FIELD_CARD16(hskew);
        REQUEST_FIELD_CARD16(vdisplay);
        REQUEST_FIELD_CARD16(vsyncstart);
        REQUEST_FIELD_CARD16(vsyncend);
        REQUEST_FIELD_CARD16(vtotal);
        REQUEST_FIELD_CARD32(flags);
        REQUEST_FIELD_CARD32(privsize);
        REQUEST_REST_CARD32();

        len =
            client->req_len -
            bytes_to_int32(sizeof(xXF86VidModeDeleteModeLineReq));
        if (len != stuff->privsize) {
            DebugF("req_len = %ld, sizeof(Req) = %d, privsize = %ld, "
                   "len = %d, length = %d\n",
                   (unsigned long) client->req_len,
                   (int) sizeof(xXF86VidModeDeleteModeLineReq) >> 2,
                   (unsigned long) stuff->privsize, len, client->req_len);
            return BadLength;
        }
        return VidModeDeleteModeLine(client, stuff);
    }
}

static int
VidModeDeleteModeLine(ClientPtr client, xXF86VidModeDeleteModeLineReq* stuff)
{
    int dotClock;
    DisplayModePtr mode;
    VidModePtr pVidMode;
    ScreenPtr pScreen;

    DebugF("DeleteModeLine - scrn: %d clock: %ld\n",
           (int) stuff->screen, (unsigned long) stuff->dotclock);
    DebugF("                 hdsp: %d hbeg: %d hend: %d httl: %d\n",
           stuff->hdisplay, stuff->hsyncstart,
           stuff->hsyncend, stuff->htotal);
    DebugF("                 vdsp: %d vbeg: %d vend: %d vttl: %d flags: %ld\n",
           stuff->vdisplay, stuff->vsyncstart, stuff->vsyncend, stuff->vtotal,
           (unsigned long) stuff->flags);

    if (stuff->screen >= screenInfo.numScreens)
        return BadValue;
    pScreen = screenInfo.screens[stuff->screen];

    pVidMode = VidModeGetPtr(pScreen);
    if (pVidMode == NULL)
        return BadImplementation;

    if (!pVidMode->GetCurrentModeline(pScreen, &mode, &dotClock))
        return BadValue;

    DebugF("Checking against clock: %d (%d)\n",
           VidModeGetModeValue(mode, VIDMODE_CLOCK), dotClock);
    DebugF("                 hdsp: %d hbeg: %d hend: %d httl: %d\n",
           VidModeGetModeValue(mode, VIDMODE_H_DISPLAY),
           VidModeGetModeValue(mode, VIDMODE_H_SYNCSTART),
           VidModeGetModeValue(mode, VIDMODE_H_SYNCEND),
           VidModeGetModeValue(mode, VIDMODE_H_TOTAL));
    DebugF("                 vdsp: %d vbeg: %d vend: %d vttl: %d flags: %d\n",
           VidModeGetModeValue(mode, VIDMODE_V_DISPLAY),
           VidModeGetModeValue(mode, VIDMODE_V_SYNCSTART),
           VidModeGetModeValue(mode, VIDMODE_V_SYNCEND),
           VidModeGetModeValue(mode, VIDMODE_V_TOTAL),
           VidModeGetModeValue(mode, VIDMODE_FLAGS));

    if ((pVidMode->GetDotClock(pScreen, stuff->dotclock) == dotClock) &&
        MODEMATCH(mode, stuff))
        return BadValue;

    if (!pVidMode->GetFirstModeline(pScreen, &mode, &dotClock))
        return BadValue;

    do {
        DebugF("Checking against clock: %d (%d)\n",
               VidModeGetModeValue(mode, VIDMODE_CLOCK), dotClock);
        DebugF("                 hdsp: %d hbeg: %d hend: %d httl: %d\n",
               VidModeGetModeValue(mode, VIDMODE_H_DISPLAY),
               VidModeGetModeValue(mode, VIDMODE_H_SYNCSTART),
               VidModeGetModeValue(mode, VIDMODE_H_SYNCEND),
               VidModeGetModeValue(mode, VIDMODE_H_TOTAL));
        DebugF("                 vdsp: %d vbeg: %d vend: %d vttl: %d flags: %d\n",
               VidModeGetModeValue(mode, VIDMODE_V_DISPLAY),
               VidModeGetModeValue(mode, VIDMODE_V_SYNCSTART),
               VidModeGetModeValue(mode, VIDMODE_V_SYNCEND),
               VidModeGetModeValue(mode, VIDMODE_V_TOTAL),
               VidModeGetModeValue(mode, VIDMODE_FLAGS));

        if ((pVidMode->GetDotClock(pScreen, stuff->dotclock) == dotClock) &&
            MODEMATCH(mode, stuff)) {
            pVidMode->DeleteModeline(pScreen, mode);
            DebugF("DeleteModeLine - Succeeded\n");
            return Success;
        }
    } while (pVidMode->GetNextModeline(pScreen, &mode, &dotClock));

    return BadValue;
}

static int
VidModeModModeLine(ClientPtr client, xXF86VidModeModModeLineReq *stuff);

static int
ProcVidModeModModeLine(ClientPtr client)
{
    /* limited to local-only connections */
    if (!VidModeAllowNonLocal && !client->local)
        return VidModeErrorBase + XF86VidModeClientNotLocal;

    DEBUG_P("XF86VidModeModModeline");

    if (ClientMajorVersion(client) < 2) {
        REQUEST_HEAD_AT_LEAST(xXF86OldVidModeModModeLineReq)
        REQUEST_FIELD_CARD32(screen);
        REQUEST_FIELD_CARD16(hdisplay);
        REQUEST_FIELD_CARD16(hsyncstart);
        REQUEST_FIELD_CARD16(hsyncend);
        REQUEST_FIELD_CARD16(htotal);
        REQUEST_FIELD_CARD16(vdisplay);
        REQUEST_FIELD_CARD16(vsyncstart);
        REQUEST_FIELD_CARD16(vsyncend);
        REQUEST_FIELD_CARD16(vtotal);
        REQUEST_FIELD_CARD32(flags);
        REQUEST_FIELD_CARD32(privsize);
        REQUEST_REST_CARD32();

        int len =
            client->req_len -
            bytes_to_int32(sizeof(xXF86OldVidModeModModeLineReq));
        if (len != stuff->privsize)
            return BadLength;

        /* convert from old format */
        xXF86VidModeModModeLineReq newstuff = {
            .length = client->req_len,
            .screen = stuff->screen,
            .hdisplay = stuff->hdisplay,
            .hsyncstart = stuff->hsyncstart,
            .hsyncend = stuff->hsyncend,
            .htotal = stuff->htotal,
            .hskew = 0,
            .vdisplay = stuff->vdisplay,
            .vsyncstart = stuff->vsyncstart,
            .vsyncend = stuff->vsyncend,
            .vtotal = stuff->vtotal,
            .flags = stuff->flags,
            .privsize = stuff->privsize,
        };
        return VidModeModModeLine(client, &newstuff);
    }
    else {
        REQUEST_HEAD_AT_LEAST(xXF86VidModeModModeLineReq);
        REQUEST_FIELD_CARD32(screen);
        REQUEST_FIELD_CARD16(hdisplay);
        REQUEST_FIELD_CARD16(hsyncstart);
        REQUEST_FIELD_CARD16(hsyncend);
        REQUEST_FIELD_CARD16(htotal);
        REQUEST_FIELD_CARD16(hskew);
        REQUEST_FIELD_CARD16(vdisplay);
        REQUEST_FIELD_CARD16(vsyncstart);
        REQUEST_FIELD_CARD16(vsyncend);
        REQUEST_FIELD_CARD16(vtotal);
        REQUEST_FIELD_CARD32(flags);
        REQUEST_FIELD_CARD32(privsize);
        REQUEST_REST_CARD32();

        int len =
            client->req_len -
            bytes_to_int32(sizeof(xXF86VidModeModModeLineReq));
        if (len != stuff->privsize)
            return BadLength;
        return VidModeModModeLine(client, stuff);
    }
}

static int
VidModeModModeLine(ClientPtr client, xXF86VidModeModModeLineReq *stuff)
{
    ScreenPtr pScreen;
    VidModePtr pVidMode;
    DisplayModePtr mode;
    int dotClock;

    DebugF("ModModeLine - scrn: %d hdsp: %d hbeg: %d hend: %d httl: %d\n",
           (int) stuff->screen, stuff->hdisplay, stuff->hsyncstart,
           stuff->hsyncend, stuff->htotal);
    DebugF("              vdsp: %d vbeg: %d vend: %d vttl: %d flags: %ld\n",
           stuff->vdisplay, stuff->vsyncstart, stuff->vsyncend,
           stuff->vtotal, (unsigned long) stuff->flags);

    if (stuff->hsyncstart < stuff->hdisplay ||
        stuff->hsyncend < stuff->hsyncstart ||
        stuff->htotal < stuff->hsyncend ||
        stuff->vsyncstart < stuff->vdisplay ||
        stuff->vsyncend < stuff->vsyncstart || stuff->vtotal < stuff->vsyncend)
        return BadValue;

    if (stuff->screen >= screenInfo.numScreens)
        return BadValue;
    pScreen = screenInfo.screens[stuff->screen];

    pVidMode = VidModeGetPtr(pScreen);
    if (pVidMode == NULL)
        return BadImplementation;

    if (!pVidMode->GetCurrentModeline(pScreen, &mode, &dotClock))
        return BadValue;

    DisplayModePtr modetmp = VidModeCreateMode();
    if (!modetmp)
        return BadAlloc;

    VidModeCopyMode(mode, modetmp);

    VidModeSetModeValue(modetmp, VIDMODE_H_DISPLAY, stuff->hdisplay);
    VidModeSetModeValue(modetmp, VIDMODE_H_SYNCSTART, stuff->hsyncstart);
    VidModeSetModeValue(modetmp, VIDMODE_H_SYNCEND, stuff->hsyncend);
    VidModeSetModeValue(modetmp, VIDMODE_H_TOTAL, stuff->htotal);
    VidModeSetModeValue(modetmp, VIDMODE_H_SKEW, stuff->hskew);
    VidModeSetModeValue(modetmp, VIDMODE_V_DISPLAY, stuff->vdisplay);
    VidModeSetModeValue(modetmp, VIDMODE_V_SYNCSTART, stuff->vsyncstart);
    VidModeSetModeValue(modetmp, VIDMODE_V_SYNCEND, stuff->vsyncend);
    VidModeSetModeValue(modetmp, VIDMODE_V_TOTAL, stuff->vtotal);
    VidModeSetModeValue(modetmp, VIDMODE_FLAGS, stuff->flags);

    if (stuff->privsize)
        DebugF("ModModeLine - Privates in request have been ignored\n");

    /* Check that the mode is consistent with the monitor specs */
    switch (pVidMode->CheckModeForMonitor(pScreen, modetmp)) {
    case MODE_OK:
        break;
    case MODE_HSYNC:
    case MODE_H_ILLEGAL:
        free(modetmp);
        return VidModeErrorBase + XF86VidModeBadHTimings;
    case MODE_VSYNC:
    case MODE_V_ILLEGAL:
        free(modetmp);
        return VidModeErrorBase + XF86VidModeBadVTimings;
    default:
        free(modetmp);
        return VidModeErrorBase + XF86VidModeModeUnsuitable;
    }

    /* Check that the driver is happy with the mode */
    if (pVidMode->CheckModeForDriver(pScreen, modetmp) != MODE_OK) {
        free(modetmp);
        return VidModeErrorBase + XF86VidModeModeUnsuitable;
    }
    free(modetmp);

    VidModeSetModeValue(mode, VIDMODE_H_DISPLAY, stuff->hdisplay);
    VidModeSetModeValue(mode, VIDMODE_H_SYNCSTART, stuff->hsyncstart);
    VidModeSetModeValue(mode, VIDMODE_H_SYNCEND, stuff->hsyncend);
    VidModeSetModeValue(mode, VIDMODE_H_TOTAL, stuff->htotal);
    VidModeSetModeValue(mode, VIDMODE_H_SKEW, stuff->hskew);
    VidModeSetModeValue(mode, VIDMODE_V_DISPLAY, stuff->vdisplay);
    VidModeSetModeValue(mode, VIDMODE_V_SYNCSTART, stuff->vsyncstart);
    VidModeSetModeValue(mode, VIDMODE_V_SYNCEND, stuff->vsyncend);
    VidModeSetModeValue(mode, VIDMODE_V_TOTAL, stuff->vtotal);
    VidModeSetModeValue(mode, VIDMODE_FLAGS, stuff->flags);

    pVidMode->SetCrtcForMode(pScreen, mode);
    pVidMode->SwitchMode(pScreen, mode);

    DebugF("ModModeLine - Succeeded\n");
    return Success;
}

static int
VidModeValidateModeLine(ClientPtr client, xXF86VidModeValidateModeLineReq *stuff);

static int
ProcVidModeValidateModeLine(ClientPtr client)
{
    int len;

    DEBUG_P("XF86VidModeValidateModeline");

    if (ClientMajorVersion(client) < 2) {
        REQUEST_HEAD_AT_LEAST(xXF86OldVidModeValidateModeLineReq);
        REQUEST_FIELD_CARD32(screen);
        REQUEST_FIELD_CARD16(hdisplay);
        REQUEST_FIELD_CARD16(hsyncstart);
        REQUEST_FIELD_CARD16(hsyncend);
        REQUEST_FIELD_CARD16(htotal);
        REQUEST_FIELD_CARD16(vdisplay);
        REQUEST_FIELD_CARD16(vsyncstart);
        REQUEST_FIELD_CARD16(vsyncend);
        REQUEST_FIELD_CARD16(vtotal);
        REQUEST_FIELD_CARD32(flags);
        REQUEST_FIELD_CARD32(privsize);
        REQUEST_REST_CARD32();

        len = client->req_len -
            bytes_to_int32(sizeof(xXF86OldVidModeValidateModeLineReq));
        if (len != stuff->privsize)
            return BadLength;

        xXF86VidModeValidateModeLineReq newstuff = {
            .length = client->req_len,
            .screen = stuff->screen,
            .dotclock = stuff->dotclock,
            .hdisplay = stuff->hdisplay,
            .hsyncstart = stuff->hsyncstart,
            .hsyncend = stuff->hsyncend,
            .htotal = stuff->htotal,
            .hskew = 0,
            .vdisplay = stuff->vdisplay,
            .vsyncstart = stuff->vsyncstart,
            .vsyncend = stuff->vsyncend,
            .vtotal = stuff->vtotal,
            .flags = stuff->flags,
            .privsize = stuff->privsize,
        };
        return VidModeValidateModeLine(client, &newstuff);
    }
    else {
        REQUEST_HEAD_AT_LEAST(xXF86VidModeValidateModeLineReq);
        REQUEST_FIELD_CARD32(screen);
        REQUEST_FIELD_CARD16(hdisplay);
        REQUEST_FIELD_CARD16(hsyncstart);
        REQUEST_FIELD_CARD16(hsyncend);
        REQUEST_FIELD_CARD16(htotal);
        REQUEST_FIELD_CARD16(hskew);
        REQUEST_FIELD_CARD16(vdisplay);
        REQUEST_FIELD_CARD16(vsyncstart);
        REQUEST_FIELD_CARD16(vsyncend);
        REQUEST_FIELD_CARD16(vtotal);
        REQUEST_FIELD_CARD32(flags);
        REQUEST_FIELD_CARD32(privsize);
        REQUEST_REST_CARD32();

        len =
            client->req_len -
            bytes_to_int32(sizeof(xXF86VidModeValidateModeLineReq));
        if (len != stuff->privsize)
            return BadLength;
        return VidModeValidateModeLine(client, stuff);
    }
}

static int
VidModeValidateModeLine(ClientPtr client, xXF86VidModeValidateModeLineReq *stuff)
{
    ScreenPtr pScreen;
    VidModePtr pVidMode;
    DisplayModePtr mode, modetmp = NULL;
    int status, dotClock;

    DebugF("ValidateModeLine - scrn: %d clock: %ld\n",
           (int) stuff->screen, (unsigned long) stuff->dotclock);
    DebugF("                   hdsp: %d hbeg: %d hend: %d httl: %d\n",
           stuff->hdisplay, stuff->hsyncstart,
           stuff->hsyncend, stuff->htotal);
    DebugF("                   vdsp: %d vbeg: %d vend: %d vttl: %d flags: %ld\n",
           stuff->vdisplay, stuff->vsyncstart, stuff->vsyncend, stuff->vtotal,
           (unsigned long) stuff->flags);

    if (stuff->screen >= screenInfo.numScreens)
        return BadValue;
    pScreen = screenInfo.screens[stuff->screen];

    status = MODE_OK;

    if (stuff->hsyncstart < stuff->hdisplay ||
        stuff->hsyncend < stuff->hsyncstart ||
        stuff->htotal < stuff->hsyncend ||
        stuff->vsyncstart < stuff->vdisplay ||
        stuff->vsyncend < stuff->vsyncstart ||
        stuff->vtotal < stuff->vsyncend) {
        status = MODE_BAD;
        goto status_reply;
    }

    pVidMode = VidModeGetPtr(pScreen);
    if (pVidMode == NULL)
        return BadImplementation;

    if (!pVidMode->GetCurrentModeline(pScreen, &mode, &dotClock))
        return BadValue;

    modetmp = VidModeCreateMode();
    if (!modetmp)
        return BadAlloc;

    VidModeCopyMode(mode, modetmp);

    VidModeSetModeValue(modetmp, VIDMODE_H_DISPLAY, stuff->hdisplay);
    VidModeSetModeValue(modetmp, VIDMODE_H_SYNCSTART, stuff->hsyncstart);
    VidModeSetModeValue(modetmp, VIDMODE_H_SYNCEND, stuff->hsyncend);
    VidModeSetModeValue(modetmp, VIDMODE_H_TOTAL, stuff->htotal);
    VidModeSetModeValue(modetmp, VIDMODE_H_SKEW, stuff->hskew);
    VidModeSetModeValue(modetmp, VIDMODE_V_DISPLAY, stuff->vdisplay);
    VidModeSetModeValue(modetmp, VIDMODE_V_SYNCSTART, stuff->vsyncstart);
    VidModeSetModeValue(modetmp, VIDMODE_V_SYNCEND, stuff->vsyncend);
    VidModeSetModeValue(modetmp, VIDMODE_V_TOTAL, stuff->vtotal);
    VidModeSetModeValue(modetmp, VIDMODE_FLAGS, stuff->flags);
    if (stuff->privsize)
        DebugF("ValidateModeLine - Privates in request have been ignored\n");

    /* Check that the mode is consistent with the monitor specs */
    if ((status =
         pVidMode->CheckModeForMonitor(pScreen, modetmp)) != MODE_OK)
        goto status_reply;

    /* Check that the driver is happy with the mode */
    status = pVidMode->CheckModeForDriver(pScreen, modetmp);

 status_reply:
    free(modetmp);

    xXF86VidModeValidateModeLineReply rep = {
        .length = bytes_to_int32(sizeof(xXF86VidModeValidateModeLineReply)
                                 - sizeof(xGenericReply)),
        .status = status
    };

    DebugF("ValidateModeLine - Succeeded (status = %d)\n", status);

    REPLY_FIELD_CARD32(status);
    REPLY_SEND();
    return Success;
}

static int
ProcVidModeSwitchMode(ClientPtr client)
{
    REQUEST_HEAD_STRUCT(xXF86VidModeSwitchModeReq);
    REQUEST_FIELD_CARD16(screen);
    REQUEST_FIELD_CARD16(zoom);

    ScreenPtr pScreen;
    VidModePtr pVidMode;

    DEBUG_P("XF86VidModeSwitchMode");

    /* limited to local-only connections */
    if (!VidModeAllowNonLocal && !client->local)
        return VidModeErrorBase + XF86VidModeClientNotLocal;

    if (stuff->screen >= screenInfo.numScreens)
        return BadValue;
    pScreen = screenInfo.screens[stuff->screen];

    pVidMode = VidModeGetPtr(pScreen);
    if (pVidMode == NULL)
        return BadImplementation;

    pVidMode->ZoomViewport(pScreen, (short) stuff->zoom);

    return Success;
}

static int
VidModeSwitchToMode(ClientPtr client, xXF86VidModeSwitchToModeReq *stuff);

static int
ProcVidModeSwitchToMode(ClientPtr client)
{
    int len;

    DEBUG_P("XF86VidModeSwitchToMode");

    /* limited to local-only connections */
    if (!VidModeAllowNonLocal && !client->local)
        return VidModeErrorBase + XF86VidModeClientNotLocal;

    if (ClientMajorVersion(client) < 2) {
        REQUEST_HEAD_AT_LEAST(xXF86OldVidModeSwitchToModeReq);
        REQUEST_FIELD_CARD32(screen);

        len =
            client->req_len -
            bytes_to_int32(sizeof(xXF86OldVidModeSwitchToModeReq));
        if (len != stuff->privsize)
            return BadLength;

        /* convert from old format */
        xXF86VidModeSwitchToModeReq newstuff = {
            .length = client->req_len,
            .screen = stuff->screen,
            .dotclock = stuff->dotclock,
            .hdisplay = stuff->hdisplay,
            .hsyncstart = stuff->hsyncstart,
            .hsyncend = stuff->hsyncend,
            .htotal = stuff->htotal,
            .vdisplay = stuff->vdisplay,
            .vsyncstart = stuff->vsyncstart,
            .vsyncend = stuff->vsyncend,
            .vtotal = stuff->vtotal,
            .flags = stuff->flags,
            .privsize = stuff->privsize,
        };
        return VidModeSwitchToMode(client, &newstuff);
    }
    else {
        REQUEST_HEAD_AT_LEAST(xXF86VidModeSwitchToModeReq);
        REQUEST_FIELD_CARD32(screen);

        len =
            client->req_len -
            bytes_to_int32(sizeof(xXF86VidModeSwitchToModeReq));
        if (len != stuff->privsize)
            return BadLength;
        return VidModeSwitchToMode(client, stuff);
    }
}

static int
VidModeSwitchToMode(ClientPtr client, xXF86VidModeSwitchToModeReq *stuff)
{
    ScreenPtr pScreen;
    VidModePtr pVidMode;
    DisplayModePtr mode;
    int dotClock;

    DebugF("SwitchToMode - scrn: %d clock: %ld\n",
           (int) stuff->screen, (unsigned long) stuff->dotclock);
    DebugF("               hdsp: %d hbeg: %d hend: %d httl: %d\n",
           stuff->hdisplay, stuff->hsyncstart,
           stuff->hsyncend, stuff->htotal);
    DebugF("               vdsp: %d vbeg: %d vend: %d vttl: %d flags: %ld\n",
           stuff->vdisplay, stuff->vsyncstart, stuff->vsyncend, stuff->vtotal,
           (unsigned long) stuff->flags);

    if (stuff->screen >= screenInfo.numScreens)
        return BadValue;
    pScreen = screenInfo.screens[stuff->screen];

    pVidMode = VidModeGetPtr(pScreen);
    if (pVidMode == NULL)
        return BadImplementation;

    if (!pVidMode->GetCurrentModeline(pScreen, &mode, &dotClock))
        return BadValue;

    if ((pVidMode->GetDotClock(pScreen, stuff->dotclock) == dotClock)
        && MODEMATCH(mode, stuff))
        return Success;

    if (!pVidMode->GetFirstModeline(pScreen, &mode, &dotClock))
        return BadValue;

    do {
        DebugF("Checking against clock: %d (%d)\n",
               VidModeGetModeValue(mode, VIDMODE_CLOCK), dotClock);
        DebugF("                 hdsp: %d hbeg: %d hend: %d httl: %d\n",
               VidModeGetModeValue(mode, VIDMODE_H_DISPLAY),
               VidModeGetModeValue(mode, VIDMODE_H_SYNCSTART),
               VidModeGetModeValue(mode, VIDMODE_H_SYNCEND),
               VidModeGetModeValue(mode, VIDMODE_H_TOTAL));
        DebugF("                 vdsp: %d vbeg: %d vend: %d vttl: %d flags: %d\n",
               VidModeGetModeValue(mode, VIDMODE_V_DISPLAY),
               VidModeGetModeValue(mode, VIDMODE_V_SYNCSTART),
               VidModeGetModeValue(mode, VIDMODE_V_SYNCEND),
               VidModeGetModeValue(mode, VIDMODE_V_TOTAL),
               VidModeGetModeValue(mode, VIDMODE_FLAGS));

        if ((pVidMode->GetDotClock(pScreen, stuff->dotclock) == dotClock) &&
            MODEMATCH(mode, stuff)) {

            if (!pVidMode->SwitchMode(pScreen, mode))
                return BadValue;

            DebugF("SwitchToMode - Succeeded\n");
            return Success;
        }
    } while (pVidMode->GetNextModeline(pScreen, &mode, &dotClock));

    return BadValue;
}

static int
ProcVidModeLockModeSwitch(ClientPtr client)
{
    REQUEST_HEAD_STRUCT(xXF86VidModeLockModeSwitchReq);
    REQUEST_FIELD_CARD16(screen);
    REQUEST_FIELD_CARD16(lock);

    ScreenPtr pScreen;
    VidModePtr pVidMode;

    DEBUG_P("XF86VidModeLockModeSwitch");

    /* limited to local-only connections */
    if (!VidModeAllowNonLocal && !client->local)
        return VidModeErrorBase + XF86VidModeClientNotLocal;

    if (stuff->screen >= screenInfo.numScreens)
        return BadValue;
    pScreen = screenInfo.screens[stuff->screen];

    pVidMode = VidModeGetPtr(pScreen);
    if (pVidMode == NULL)
        return BadImplementation;

    if (!pVidMode->LockZoom(pScreen, (short) stuff->lock))
        return VidModeErrorBase + XF86VidModeZoomLocked;

    return Success;
}

static inline CARD32 _combine_f(vidMonitorValue a, vidMonitorValue b, Bool swapped)
{
    CARD32 buf =
        ((unsigned short) a.f) |
        ((unsigned short) b.f << 16);
    if (swapped)
        swapl(&buf);
    return buf;
}

static int
ProcVidModeGetMonitor(ClientPtr client)
{
    REQUEST_HEAD_STRUCT(xXF86VidModeGetMonitorReq);
    REQUEST_FIELD_CARD16(screen);

    DEBUG_P("XF86VidModeGetMonitor");

    if (stuff->screen >= screenInfo.numScreens)
        return BadValue;
    ScreenPtr pScreen = screenInfo.screens[stuff->screen];

    VidModePtr pVidMode = VidModeGetPtr(pScreen);
    if (pVidMode == NULL)
        return BadImplementation;

    const int nHsync = pVidMode->GetMonitorValue(pScreen, VIDMODE_MON_NHSYNC, 0).i;
    const int nVrefresh = pVidMode->GetMonitorValue(pScreen, VIDMODE_MON_NVREFRESH, 0).i;

    const char *vendorStr = (const char*)pVidMode->GetMonitorValue(pScreen, VIDMODE_MON_VENDOR, 0).ptr;
    const char *modelStr = (const char*)pVidMode->GetMonitorValue(pScreen, VIDMODE_MON_MODEL, 0).ptr;

    const int vendorLength = (vendorStr ? strlen(vendorStr) : 0);
    const int modelLength = (modelStr ? strlen(modelStr) : 0);

    const int nVendorItems = bytes_to_int32(pad_to_int32(vendorLength));
    const int nModelItems = bytes_to_int32(pad_to_int32(modelLength));

    xXF86VidModeGetMonitorReply rep = {
        .nhsync = nHsync,
        .nvsync = nVrefresh,
        .vendorLength = vendorLength,
        .modelLength = modelLength,
        .length = bytes_to_int32(sizeof(xXF86VidModeGetMonitorReply) -
                                 sizeof(xGenericReply))
                  + nHsync + nVrefresh + nVendorItems + nModelItems
    };

    const int buflen = nHsync + nVrefresh + nVendorItems + nModelItems;

    CARD32 *sendbuf = calloc(buflen, sizeof(CARD32));
    if (!sendbuf)
        return BadAlloc;

    CARD32 *bufwalk = sendbuf;

    for (int i = 0; i < nHsync; i++) {
        *bufwalk = _combine_f(pVidMode->GetMonitorValue(pScreen, VIDMODE_MON_HSYNC_LO, i),
                              pVidMode->GetMonitorValue(pScreen, VIDMODE_MON_HSYNC_HI, i),
                              client->swapped);
        bufwalk++;
    }

    for (int i = 0; i < nVrefresh; i++) {
        *bufwalk = _combine_f(pVidMode->GetMonitorValue(pScreen, VIDMODE_MON_VREFRESH_LO, i),
                              pVidMode->GetMonitorValue(pScreen, VIDMODE_MON_VREFRESH_HI, i),
                              client->swapped);
        bufwalk++;
    }

    memcpy(bufwalk,
           pVidMode->GetMonitorValue(pScreen, VIDMODE_MON_VENDOR, 0).ptr,
           vendorLength);
    bufwalk += nVendorItems;

    memcpy(bufwalk,
           pVidMode->GetMonitorValue(pScreen, VIDMODE_MON_MODEL, 0).ptr,
           modelLength);
    bufwalk += nModelItems;

    if (client->swapped) {
        swaps(&rep.sequenceNumber);
        swapl(&rep.length);
    }

    REPLY_SEND_EXTRA(sendbuf, buflen * sizeof(CARD32));

    free(sendbuf);
    return Success;
}

static int
ProcVidModeGetViewPort(ClientPtr client)
{
    REQUEST_HEAD_STRUCT(xXF86VidModeGetViewPortReq);
    REQUEST_FIELD_CARD16(screen);

    ScreenPtr pScreen;
    VidModePtr pVidMode;
    int x, y;

    DEBUG_P("XF86VidModeGetViewPort");

    if (stuff->screen >= screenInfo.numScreens)
        return BadValue;
    pScreen = screenInfo.screens[stuff->screen];

    pVidMode = VidModeGetPtr(pScreen);
    if (pVidMode == NULL)
        return BadImplementation;

    pVidMode->GetViewPort(pScreen, &x, &y);

    xXF86VidModeGetViewPortReply rep = {
        .x = x,
        .y = y
    };

    REPLY_FIELD_CARD32(x);
    REPLY_FIELD_CARD32(y);
    REPLY_SEND();
    return Success;
}

static int
ProcVidModeSetViewPort(ClientPtr client)
{
    REQUEST_HEAD_STRUCT(xXF86VidModeSetViewPortReq);
    REQUEST_FIELD_CARD16(screen);
    REQUEST_FIELD_CARD32(x);
    REQUEST_FIELD_CARD32(y);

    ScreenPtr pScreen;
    VidModePtr pVidMode;

    DEBUG_P("XF86VidModeSetViewPort");

    /* limited to local-only connections */
    if (!VidModeAllowNonLocal && !client->local)
        return VidModeErrorBase + XF86VidModeClientNotLocal;

    if (stuff->screen >= screenInfo.numScreens)
        return BadValue;
    pScreen = screenInfo.screens[stuff->screen];

    pVidMode = VidModeGetPtr(pScreen);
    if (pVidMode == NULL)
        return BadImplementation;

    if (!pVidMode->SetViewPort(pScreen, stuff->x, stuff->y))
        return BadValue;

    return Success;
}

static int
ProcVidModeGetDotClocks(ClientPtr client)
{
    REQUEST_HEAD_STRUCT(xXF86VidModeGetDotClocksReq);
    REQUEST_FIELD_CARD16(screen);

    ScreenPtr pScreen;
    VidModePtr pVidMode;
    int n;
    int numClocks;
    CARD32 dotclock;
    int *Clocks = NULL;
    Bool ClockProg;

    DEBUG_P("XF86VidModeGetDotClocks");

    if (stuff->screen >= screenInfo.numScreens)
        return BadValue;
    pScreen = screenInfo.screens[stuff->screen];

    pVidMode = VidModeGetPtr(pScreen);
    if (pVidMode == NULL)
        return BadImplementation;

    numClocks = pVidMode->GetNumOfClocks(pScreen, &ClockProg);

    if (!ClockProg) {
        Clocks = calloc(numClocks, sizeof(int));
        if (!Clocks)
            return BadValue;
        if (!pVidMode->GetClocks(pScreen, Clocks)) {
            free(Clocks);
            return BadValue;
        }
    }

    xXF86VidModeGetDotClocksReply rep = {
        .length = bytes_to_int32(sizeof(xXF86VidModeGetDotClocksReply)
                                 - sizeof(xGenericReply)) + numClocks,
        .clocks = numClocks,
        .maxclocks = MAXCLOCKS,
        .flags = (ClockProg ? CLKFLAG_PROGRAMABLE : 0),
    };

    REPLY_FIELD_CARD32(clocks);
    REPLY_FIELD_CARD32(maxclocks);
    REPLY_FIELD_CARD32(flags);
    REPLY_SEND();

    if (!ClockProg && Clocks) {
        for (n = 0; n < numClocks; n++) {
            dotclock = Clocks[n];
            WriteSwappedDataToClient(client, 4, (char *) &dotclock);
        }
    }

    free(Clocks);
    return Success;
}

static int
ProcVidModeSetGamma(ClientPtr client)
{
    REQUEST_HEAD_STRUCT(xXF86VidModeSetGammaReq);
    REQUEST_FIELD_CARD16(screen);
    REQUEST_FIELD_CARD32(red);
    REQUEST_FIELD_CARD32(green);
    REQUEST_FIELD_CARD32(blue);

    ScreenPtr pScreen;
    VidModePtr pVidMode;

    DEBUG_P("XF86VidModeSetGamma");

    /* limited to local-only connections */
    if (!VidModeAllowNonLocal && !client->local)
        return VidModeErrorBase + XF86VidModeClientNotLocal;

    if (stuff->screen >= screenInfo.numScreens)
        return BadValue;
    pScreen = screenInfo.screens[stuff->screen];

    pVidMode = VidModeGetPtr(pScreen);
    if (pVidMode == NULL)
        return BadImplementation;

    if (!pVidMode->SetGamma(pScreen, ((float) stuff->red) / 10000.,
                         ((float) stuff->green) / 10000.,
                         ((float) stuff->blue) / 10000.))
        return BadValue;

    return Success;
}

static int
ProcVidModeGetGamma(ClientPtr client)
{
    REQUEST_HEAD_STRUCT(xXF86VidModeGetGammaReq);
    REQUEST_FIELD_CARD16(screen);

    ScreenPtr pScreen;
    VidModePtr pVidMode;
    float red, green, blue;

    DEBUG_P("XF86VidModeGetGamma");

    if (stuff->screen >= screenInfo.numScreens)
        return BadValue;
    pScreen = screenInfo.screens[stuff->screen];

    pVidMode = VidModeGetPtr(pScreen);
    if (pVidMode == NULL)
        return BadImplementation;

    if (!pVidMode->GetGamma(pScreen, &red, &green, &blue))
        return BadValue;

    xXF86VidModeGetGammaReply rep = {
        .red = (CARD32) (red * 10000.),
        .green = (CARD32) (green * 10000.),
        .blue = (CARD32) (blue * 10000.)
    };

    REPLY_FIELD_CARD32(red);
    REPLY_FIELD_CARD32(green);
    REPLY_FIELD_CARD32(blue);
    REPLY_SEND();
    return Success;
}

static int
ProcVidModeSetGammaRamp(ClientPtr client)
{
    REQUEST_HEAD_AT_LEAST(xXF86VidModeSetGammaRampReq);
    REQUEST_FIELD_CARD16(size);
    REQUEST_FIELD_CARD16(screen);

    int l = ((stuff->size + 1) & ~1) * 6;
    REQUEST_FIXED_SIZE(xXF86VidModeSetGammaRampReq, l);
    REQUEST_REST_CARD16();

    CARD16 *r, *g, *b;
    int length;
    ScreenPtr pScreen;
    VidModePtr pVidMode;

    /* limited to local-only connections */
    if (!VidModeAllowNonLocal && !client->local)
        return VidModeErrorBase + XF86VidModeClientNotLocal;

    if (stuff->screen >= screenInfo.numScreens)
        return BadValue;
    pScreen = screenInfo.screens[stuff->screen];

    pVidMode = VidModeGetPtr(pScreen);
    if (pVidMode == NULL)
        return BadImplementation;

    if (stuff->size != pVidMode->GetGammaRampSize(pScreen))
        return BadValue;

    length = (stuff->size + 1) & ~1;

    REQUEST_FIXED_SIZE(xXF86VidModeSetGammaRampReq, length * 6);

    r = (CARD16 *) &stuff[1];
    g = r + length;
    b = g + length;

    if (!pVidMode->SetGammaRamp(pScreen, stuff->size, r, g, b))
        return BadValue;

    return Success;
}

static int
ProcVidModeGetGammaRamp(ClientPtr client)
{
    REQUEST_HEAD_STRUCT(xXF86VidModeGetGammaRampReq);
    REQUEST_FIELD_CARD16(size);
    REQUEST_FIELD_CARD16(screen);

    CARD16 *ramp = NULL;
    size_t ramplen = 0;

    if (stuff->screen >= screenInfo.numScreens)
        return BadValue;
    ScreenPtr pScreen = screenInfo.screens[stuff->screen];

    VidModePtr pVidMode = VidModeGetPtr(pScreen);
    if (pVidMode == NULL)
        return BadImplementation;

    if (stuff->size != pVidMode->GetGammaRampSize(pScreen))
        return BadValue;

    const int length = (stuff->size + 1) & ~1;

    if (stuff->size) {
        if (!(ramp = calloc(length, 3 * sizeof(CARD16))))
            return BadAlloc;
        ramplen = length * 3 * sizeof(CARD16);

        if (!pVidMode->GetGammaRamp(pScreen, stuff->size,
                                 ramp, ramp + length, ramp + (length * 2))) {
            free(ramp);
            return BadValue;
        }
    }

    xXF86VidModeGetGammaRampReply rep = {
        .size = stuff->size
    };

    REPLY_BUF_CARD16((short *) ramp, length * 3);
    REPLY_FIELD_CARD16(size);
    REPLY_SEND_EXTRA(ramp, ramplen);
    free(ramp);
    return Success;
}


static int
ProcVidModeGetGammaRampSize(ClientPtr client)
{
    REQUEST_HEAD_STRUCT(xXF86VidModeGetGammaRampSizeReq);
    REQUEST_FIELD_CARD16(screen);

    ScreenPtr pScreen;
    VidModePtr pVidMode;

    if (stuff->screen >= screenInfo.numScreens)
        return BadValue;
    pScreen = screenInfo.screens[stuff->screen];

    pVidMode = VidModeGetPtr(pScreen);
    if (pVidMode == NULL)
        return BadImplementation;

    xXF86VidModeGetGammaRampSizeReply rep = {
        .size = pVidMode->GetGammaRampSize(pScreen)
    };
    REPLY_FIELD_CARD16(size);
    REPLY_SEND();
    return Success;
}

static int
ProcVidModeGetPermissions(ClientPtr client)
{
    REQUEST_HEAD_STRUCT(xXF86VidModeGetPermissionsReq);
    REQUEST_FIELD_CARD16(screen);

    if (stuff->screen >= screenInfo.numScreens)
        return BadValue;

    xXF86VidModeGetPermissionsReply rep =  {
        .permissions = (XF86VM_READ_PERMISSION |
                        ((VidModeAllowNonLocal || client->local) ?
                            XF86VM_WRITE_PERMISSION : 0)),
    };

    REPLY_FIELD_CARD32(permissions);
    REPLY_SEND();
    return Success;
}

static int
ProcVidModeSetClientVersion(ClientPtr client)
{
    REQUEST_HEAD_STRUCT(xXF86VidModeSetClientVersionReq);
    REQUEST_FIELD_CARD16(major);
    REQUEST_FIELD_CARD16(minor);

    VidModePrivPtr pPriv;

    DEBUG_P("XF86VidModeSetClientVersion");

    if ((pPriv = VM_GETPRIV(client)) == NULL) {
        pPriv = calloc(1, sizeof(VidModePrivRec));
        if (!pPriv)
            return BadAlloc;
        VM_SETPRIV(client, pPriv);
    }
    pPriv->major = stuff->major;

    pPriv->minor = stuff->minor;

    return Success;
}


static int
ProcVidModeDispatch(ClientPtr client)
{
    REQUEST(xReq);
    switch (stuff->data) {
    case X_XF86VidModeQueryVersion:
        return ProcVidModeQueryVersion(client);
    case X_XF86VidModeGetModeLine:
        return ProcVidModeGetModeLine(client);
    case X_XF86VidModeGetMonitor:
        return ProcVidModeGetMonitor(client);
    case X_XF86VidModeGetAllModeLines:
        return ProcVidModeGetAllModeLines(client);
    case X_XF86VidModeValidateModeLine:
        return ProcVidModeValidateModeLine(client);
    case X_XF86VidModeGetViewPort:
        return ProcVidModeGetViewPort(client);
    case X_XF86VidModeGetDotClocks:
        return ProcVidModeGetDotClocks(client);
    case X_XF86VidModeSetClientVersion:
        return ProcVidModeSetClientVersion(client);
    case X_XF86VidModeGetGamma:
        return ProcVidModeGetGamma(client);
    case X_XF86VidModeGetGammaRamp:
        return ProcVidModeGetGammaRamp(client);
    case X_XF86VidModeGetGammaRampSize:
        return ProcVidModeGetGammaRampSize(client);
    case X_XF86VidModeGetPermissions:
        return ProcVidModeGetPermissions(client);
    case X_XF86VidModeAddModeLine:
        return ProcVidModeAddModeLine(client);
    case X_XF86VidModeDeleteModeLine:
        return ProcVidModeDeleteModeLine(client);
    case X_XF86VidModeModModeLine:
        return ProcVidModeModModeLine(client);
    case X_XF86VidModeSwitchMode:
        return ProcVidModeSwitchMode(client);
    case X_XF86VidModeSwitchToMode:
        return ProcVidModeSwitchToMode(client);
    case X_XF86VidModeLockModeSwitch:
        return ProcVidModeLockModeSwitch(client);
    case X_XF86VidModeSetViewPort:
        return ProcVidModeSetViewPort(client);
    case X_XF86VidModeSetGamma:
        return ProcVidModeSetGamma(client);
    case X_XF86VidModeSetGammaRamp:
        return ProcVidModeSetGammaRamp(client);
    default:
        return BadRequest;
    }
}

void
VidModeAddExtension(Bool allow_non_local)
{
    ExtensionEntry *extEntry;

    DEBUG_P("VidModeAddExtension");

    if (!dixRegisterPrivateKey(VidModeClientPrivateKey, PRIVATE_CLIENT, 0))
        return;

    if ((extEntry = AddExtension(XF86VIDMODENAME,
                                 XF86VidModeNumberEvents,
                                 XF86VidModeNumberErrors,
                                 ProcVidModeDispatch,
                                 ProcVidModeDispatch,
                                 NULL, StandardMinorOpcode))) {
        VidModeErrorBase = extEntry->errorBase;
        VidModeAllowNonLocal = allow_non_local;
    }
}

VidModePtr VidModeGetPtr(ScreenPtr pScreen)
{
    return (VidModePtr) (dixLookupPrivate(&pScreen->devPrivates, VidModePrivateKey));
}

VidModePtr VidModeInit(ScreenPtr pScreen)
{
    if (!dixRegisterPrivateKey(VidModePrivateKey, PRIVATE_SCREEN, sizeof(VidModeRec)))
        return NULL;

    return VidModeGetPtr(pScreen);
}

#endif /* XF86VIDMODE */
