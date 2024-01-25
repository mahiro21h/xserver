#include <dix-config.h>

#include <X11/Xmd.h>

#include "include/extinit_priv.h"
#include "include/os.h"

#include "namespace.h"

Bool noNamespaceExtension = TRUE;

void
NamespaceExtensionInit(void)
{
    XNS_LOG("initializing namespace extension ...\n");
}
