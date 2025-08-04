/*
 * Copyright 2007-2008 Peter Hutterer
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Author: Peter Hutterer, University of South Australia, NICTA
 */

#include <dix-config.h>

#include <X11/X.h>              /* for inputstr.h    */
#include <X11/Xproto.h>         /* Request macro     */
#include <X11/extensions/XI.h>
#include <X11/extensions/XI2proto.h>

#include "dix/dix_priv.h"
#include "dix/request_priv.h"

#include "inputstr.h"           /* DeviceIntPtr      */
#include "windowstr.h"          /* window structure  */
#include "scrnintstr.h"         /* screen structure  */
#include "extnsionst.h"
#include "exevents.h"
#include "exglobals.h"
#include "xigetclientpointer.h"

int
ProcXIGetClientPointer(ClientPtr client)
{
    REQUEST_HEAD_STRUCT(xXIGetClientPointerReq);
    REQUEST_FIELD_CARD32(win);

    ClientPtr winclient;
    if (stuff->win != None) {
        if (dixLookupResourceOwner(&winclient, stuff->win, client, DixGetAttrAccess) != Success)
            return BadWindow;
    }
    else
        winclient = client;

    xXIGetClientPointerReply rep = {
        .RepType = X_XIGetClientPointer,
        .set = (winclient->clientPtr != NULL),
        .deviceid = (winclient->clientPtr) ? winclient->clientPtr->id : 0
    };

    REPLY_FIELD_CARD16(deviceid);
    REPLY_SEND();
    return Success;
}
