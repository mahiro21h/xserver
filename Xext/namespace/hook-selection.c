#define HOOK_NAME "selection"

#include <dix-config.h>

#include <stdio.h>

#include "dix/selection_priv.h"

#include "namespace.h"
#include "hooks.h"

static inline const char *stripNS(const char* name) {
    if ((!name) || (name[0] != '<'))
        return name; // can this ever happen ?
    const char *got = strchr(name, '>');
    if (!got)
        return name;
    return ++got;
}

/*
 * This hook is rewriting the client visible selection names to internally used,
 * per namespace ones. Whenever a client is asking for a selection, it's name
 * is replaced by a namespaced one, e.g. asking for "PRIMARY" while being in
 * namespace "foo" will become "<foo>PRIMARY"
 *
 * A malicious client could still send specially crafted messages to others,
 * asking them to send their selection data to him. This needs to be solved
 * separately, by a send hook.
 */
void hookSelectionFilter(CallbackListPtr *pcbl, void *unused, void *calldata)
{
    XNS_HOOK_HEAD(SelectionFilterParamRec);

    /* no rewrite if client is in root namespace */
    if (subj->ns->superPower)
        return;

    const char *op = "<>";
    switch (param->op) {
        case SELECTION_FILTER_GETOWNER:
            op = "SELECTION_FILTER_GETOWNER";
        break;
        case SELECTION_FILTER_SETOWNER:
            op = "SELECTION_FILTER_SETOWNER";
        break;
        case SELECTION_FILTER_CONVERT:
            op = "SELECTION_FILTER_CONVERT";
        break;
        case SELECTION_FILTER_EV_REQUEST:
            op = "SELECTION_FILTER_EV_REQUEST";
        break;
        case SELECTION_FILTER_EV_CLEAR:
            op = "SELECTION_FILTER_EV_CLEAR";
        break;
    }

    const char *origSelectionName = NameForAtom(param->selection);

    char selname[PATH_MAX] = { 0 };
    snprintf(selname, sizeof(selname)-1, "<%s>%s", subj->ns->name, origSelectionName);
    Atom realSelection = MakeAtom(selname, strlen(selname), TRUE);

    switch (param->op) {
        case SELECTION_FILTER_GETOWNER:
        case SELECTION_FILTER_SETOWNER:
        case SELECTION_FILTER_CONVERT:
        case SELECTION_FILTER_LISTEN:
            XNS_HOOK_LOG("%s origsel=%s (%d) newsel=%s (%d) -- using newsel\n", op,
                origSelectionName,
                param->selection,
                selname,
                realSelection);
            // TODO: check whether window really belongs to the client
            param->selection = realSelection;
        break;

        case SELECTION_FILTER_NOTIFY:
        {
            // need to translate back, since we're having the ns-prefixed name here
            const char *stripped = stripNS(origSelectionName);
            ATOM strippedAtom = MakeAtom(stripped, strlen(stripped), TRUE);
            XNS_HOOK_LOG("NOTIFY stripped=\"%s\" atom=%d name=\"%s\n", stripped, strippedAtom, NameForAtom(strippedAtom));
            XNS_HOOK_LOG("NOTIFY origID %d origName %s transID %d transName %s strippedName %s atom %d\n",
                param->selection,
                origSelectionName,
                realSelection,
                selname,
                stripped,
                strippedAtom);
            param->selection = strippedAtom;
            break;
        }

        // nothing to do here: already having the client visible name
        case SELECTION_FILTER_EV_REQUEST:
        case SELECTION_FILTER_EV_CLEAR:
            XNS_HOOK_LOG("%s origsel=%s (%d) newsel=%s (%d) -- using origsel\n", op,
                NameForAtom(param->selection),
                param->selection,
                selname,
                realSelection);
        break;
        default:
            XNS_HOOK_LOG("unknown op: %d selection=%d\n", param->op, param->selection);
        break;
    }
}
