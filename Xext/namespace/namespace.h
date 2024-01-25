#ifndef __XSERVER_NAMESPACE_H
#define __XSERVER_NAMESPACE_H

#include <stdio.h>
#include <X11/Xmd.h>

#include "include/list.h"
#include "include/window.h"

struct Xnamespace {
    struct xorg_list entry;
    const char *name;
    Bool builtin;
    Bool superPower;
    size_t refcnt;
};

extern struct xorg_list ns_list;
extern struct Xnamespace ns_root;
extern struct Xnamespace ns_anon;

#define NS_NAME_ROOT      "root"
#define NS_NAME_ANONYMOUS "anon"

Bool XnsLoadConfig(void);
struct Xnamespace *XnsFindByName(const char* name);

#define XNS_LOG(...) do { printf("XNS "); printf(__VA_ARGS__); } while (0)

static inline Bool streq(const char *a, const char *b)
{
    if (!a && !b)
        return TRUE;
    if (!a || !b)
        return FALSE;
    return (strcmp(a,b) == 0);
}

#endif /* __XSERVER_NAMESPACE_H */
