#include <dix-config.h>

#include <stdio.h>
#include <X11/Xmd.h>

#include "include/extinit_priv.h"
#include "include/os.h"

#include "namespace.h"

Bool noNamespaceExtension = TRUE;

void
NamespaceExtensionInit(void)
{
    XNS_LOG("initializing namespace extension ...\n");

    /* load configuration */
    if (!XnsLoadConfig()) {
        XNS_LOG("No config file. disabling Xns extension\n");
        return;
    }
}
