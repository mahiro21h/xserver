#define HOOK_NAME "windowproperty"

#include <dix-config.h>

#include <X11/Xmd.h>

#include "dix/dix_priv.h"
#include "dix/property_priv.h"
#include "dix/window_priv.h"

#include "namespace.h"
#include "hooks.h"

static inline Bool winIsRoot(WindowPtr pWin) {
    if (!pWin)
        return FALSE;
    if (pWin->drawable.pScreen->root == pWin)
        return TRUE;
    return FALSE;
}

void hookWindowProperty(CallbackListPtr *pcbl, void *unused, void *calldata)
{
    XNS_HOOK_HEAD(PropertyFilterParam);

    // no redirect on super power
    if (subj->ns->superPower)
        return;

    const ClientPtr owner = dixLookupXIDOwner(param->window);
    if (!owner) {
        param->status = BadWindow;
        param->skip = TRUE;
        XNS_HOOK_LOG("owner of window 0x%0x doesn't exist\n", param->window);
        return;
    }

    // whitelist anything that goes to caller's own namespace
    struct XnamespaceClientPriv *obj = XnsClientPriv(owner);
    if (XnsClientSameNS(subj, obj)) {
        XNS_HOOK_LOG("same NS window property %s (atom 0x%x) window 0x%0x of client %d\n",
            NameForAtom(param->property),
            param->property,
            param->window,
            owner->index);
        return;
    }

    // allow access to namespace virtual root
    if (param->window == subj->ns->rootWindow->drawable.id) {
        XNS_HOOK_LOG("NS virtual root window property %s (atom 0x%x) window 0x%0x of client %d\n",
            NameForAtom(param->property),
            param->property,
            param->window,
            owner->index);
        return;
    }

    // redirect root window access to namespace's virtual root
    if (dixWindowIsRoot(param->window)) {
        XNS_HOOK_LOG("redirect root window property %s (atom 0x%x) window 0x%0x of client %d to 0x%x\n",
            NameForAtom(param->property),
            param->property,
            param->window,
            owner->index,
            subj->ns->rootWindow->drawable.id);
        param->window = subj->ns->rootWindow->drawable.id;
        return;
    }

    XNS_HOOK_LOG("foreign NS window property %s (atom 0x%x) window 0x%0x of client %d\n",
        NameForAtom(param->property),
        param->property,
        param->window,
        owner->index);
}
