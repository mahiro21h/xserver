#include <dix-config.h>

#include <stdio.h>
#include <X11/X.h>
#include <X11/Xproto.h>
#include <X11/extensions/myextensionproto.h>

#include "miext/extinit_priv.h"

#include "misc.h"
#include "os.h"
#include "protocol-versions.h"

Bool noMyextensionExtension = FALSE;

static int lockscreen(ClientPtr client);
static int unlockscreen(ClientPtr client);

static int
ProcMyextensionQueryVersion(ClientPtr client)
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

    REQUEST_SIZE_MATCH(xMyextensionLockScreenReq);
    return Success;
}

static int _X_COLD
unlockscreen(ClientPtr client) {
    if (get_ddxInfo_wrap(dontVTSwitch))
        set_ddxInfo_wrap(dontVTSwitch, FALSE);

    if (get_ddxInfo_wrap(dontZap))
        set_ddxInfo_wrap(dontZap, FALSE);

    REQUEST_SIZE_MATCH(xMyextensionUnlockScreenReq);
    return Success;
}

static int
ProcScreenSaverDispatch(ClientPtr client)
{
    REQUEST(xReq);
    switch (stuff->data) {
        case X_MyextensionQueryVersion:
            return ProcMyextensionQueryVersion(client);
        case X_MyextensionLockScreen:
            return lockscreen(client);
        case X_MyextensionUnlockScreen:
            return unlockscreen(client);
        default:
            return BadRequest;
    }
}

static int _X_COLD
SProcScreenSaverDispatch(ClientPtr client) /* TODO: add swapped versions of funcs */
{
    FatalError("todo");
    REQUEST(xReq);
    switch (stuff->data) {
        case X_MyextensionQueryVersion:
            return ProcMyextensionQueryVersion(client);
        default:
            return BadRequest;
    }
}

void
MyextensionExtensionInit(void)
{
    ExtensionEntry *extEntry;

    if ((extEntry = AddExtension(MyextensionName, MyextensionNumberEvents, 0,
                                 ProcScreenSaverDispatch,
                                 SProcScreenSaverDispatch, NULL,
                                 StandardMinorOpcode))) {
    }
}
