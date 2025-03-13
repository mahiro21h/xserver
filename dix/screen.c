/* SPDX-License-Identifier: MIT OR X11
 *
 * Copyright © 2024 Enrico Weigelt, metux IT consult <info@metux.net>
 */
#include <dix-config.h>

#include "dix/callback_priv.h"
#include "dix/dix_priv.h"
#include "dix/gc_priv.h"
#include "include/screenint.h"
#include "include/scrnintstr.h"

static void dixFreeScreen(ScreenPtr pScreen)
{
    if (!pScreen)
        return;

    FreeGCperDepth(pScreen);
    dixDestroyPixmap(pScreen->defaultStipple, 0);
    dixFreeScreenSpecificPrivates(pScreen);
    dixScreenRaiseClose(pScreen);
    dixFreePrivates(pScreen->devPrivates, PRIVATE_SCREEN);
    DeleteCallbackList(&pScreen->hookWindowDestroy);
    DeleteCallbackList(&pScreen->hookWindowPosition);
    DeleteCallbackList(&pScreen->hookClose);
    DeleteCallbackList(&pScreen->hookPostClose);
    DeleteCallbackList(&pScreen->hookPixmapDestroy);
    free(pScreen);
}

void dixFreeAllScreens(void)
{
    for (int i = screenInfo.numGPUScreens - 1; i >= 0; i--) {
        dixFreeScreen(screenInfo.gpuscreens[i]);
        screenInfo.numGPUScreens = i;
    }
    memset(&screenInfo.numGPUScreens, 0, sizeof(screenInfo.numGPUScreens));

    for (int i = screenInfo.numScreens - 1; i >= 0; i--) {
        dixFreeScreen(screenInfo.screens[i]);
        screenInfo.numScreens = i;
    }
    memset(&screenInfo.screens, 0, sizeof(screenInfo.numGPUScreens));
}
