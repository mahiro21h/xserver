
#include <dix-config.h>

#include <string.h>
#include <X11/X.h>
#include <X11/Xfuncproto.h>
#include <X11/Xproto.h>
#include <X11/extensions/XvMC.h>
#include <X11/extensions/Xvproto.h>
#include <X11/extensions/XvMCproto.h>

#include "dix/request_priv.h"
#include "dix/screen_hooks_priv.h"
#include "miext/extinit_priv.h"
#include "Xext/xvdix_priv.h"

#include "misc.h"
#include "os.h"
#include "dixstruct.h"
#include "resource.h"
#include "scrnintstr.h"
#include "extnsionst.h"
#include "servermd.h"
#include "xvmcext.h"

#ifdef HAS_XVMCSHM
#include <sys/ipc.h>
#include <sys/types.h>
#include <sys/shm.h>
#endif                          /* HAS_XVMCSHM */

#define SERVER_XVMC_MAJOR_VERSION               1
#define SERVER_XVMC_MINOR_VERSION               1

#define DR_CLIENT_DRIVER_NAME_SIZE 48
#define DR_BUSID_SIZE 48

static DevPrivateKeyRec XvMCScreenKeyRec;

#define XvMCScreenKey (&XvMCScreenKeyRec)
static Bool XvMCInUse;

int XvMCReqCode;
int XvMCEventBase;

static RESTYPE XvMCRTContext;
static RESTYPE XvMCRTSurface;
static RESTYPE XvMCRTSubpicture;

typedef struct {
    int num_adaptors;
    XvMCAdaptorPtr adaptors;
    char clientDriverName[DR_CLIENT_DRIVER_NAME_SIZE];
    char busID[DR_BUSID_SIZE];
    int major;
    int minor;
    int patchLevel;
} XvMCScreenRec, *XvMCScreenPtr;

#define XVMC_GET_PRIVATE(pScreen) \
    (XvMCScreenPtr)(dixLookupPrivate(&(pScreen)->devPrivates, XvMCScreenKey))

static int
XvMCDestroyContextRes(void *data, XID id)
{
    XvMCContextPtr pContext = (XvMCContextPtr) data;

    pContext->refcnt--;

    if (!pContext->refcnt) {
        XvMCScreenPtr pScreenPriv = XVMC_GET_PRIVATE(pContext->pScreen);

        (*pScreenPriv->adaptors[pContext->adapt_num].DestroyContext) (pContext);
        free(pContext);
    }

    return Success;
}

static int
XvMCDestroySurfaceRes(void *data, XID id)
{
    XvMCSurfacePtr pSurface = (XvMCSurfacePtr) data;
    XvMCContextPtr pContext = pSurface->context;
    XvMCScreenPtr pScreenPriv = XVMC_GET_PRIVATE(pContext->pScreen);

    (*pScreenPriv->adaptors[pContext->adapt_num].DestroySurface) (pSurface);
    free(pSurface);

    XvMCDestroyContextRes((void *) pContext, pContext->context_id);

    return Success;
}

static int
XvMCDestroySubpictureRes(void *data, XID id)
{
    XvMCSubpicturePtr pSubpict = (XvMCSubpicturePtr) data;
    XvMCContextPtr pContext = pSubpict->context;
    XvMCScreenPtr pScreenPriv = XVMC_GET_PRIVATE(pContext->pScreen);

    (*pScreenPriv->adaptors[pContext->adapt_num].DestroySubpicture) (pSubpict);
    free(pSubpict);

    XvMCDestroyContextRes((void *) pContext, pContext->context_id);

    return Success;
}

static int
ProcXvMCQueryVersion(ClientPtr client)
{
    REQUEST_HEAD_STRUCT(xvmcQueryVersionReq);

    xvmcQueryVersionReply rep = {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .major = SERVER_XVMC_MAJOR_VERSION,
        .minor = SERVER_XVMC_MINOR_VERSION
    };

    WriteToClient(client, sizeof(xvmcQueryVersionReply), &rep);
    return Success;
}

static int
ProcXvMCListSurfaceTypes(ClientPtr client)
{
    XvPortPtr pPort;
    XvMCScreenPtr pScreenPriv;
    XvMCAdaptorPtr adaptor = NULL;

    REQUEST_HEAD_STRUCT(xvmcListSurfaceTypesReq);
    REQUEST_FIELD_CARD32(port);

    VALIDATE_XV_PORT(stuff->port, pPort, DixReadAccess);

    if (XvMCInUse) {            /* any adaptors at all */
        ScreenPtr pScreen = pPort->pAdaptor->pScreen;

        if ((pScreenPriv = XVMC_GET_PRIVATE(pScreen))) {        /* any this screen */
            for (int i = 0; i < pScreenPriv->num_adaptors; i++) {
                if (pPort->pAdaptor == pScreenPriv->adaptors[i].xv_adaptor) {
                    adaptor = &(pScreenPriv->adaptors[i]);
                    break;
                }
            }
        }
    }

    int num_surfaces = (adaptor) ? adaptor->num_surfaces : 0;
    xvmcSurfaceInfo *info = NULL;
    if (num_surfaces) {
        info = calloc(sizeof(xvmcSurfaceInfo), num_surfaces);
        if (!info)
            return BadAlloc;

        for (int i = 0; i < num_surfaces; i++) {
            XvMCSurfaceInfoPtr surface = adaptor->surfaces[i];
            info[i].surface_type_id = surface->surface_type_id;
            info[i].chroma_format = surface->chroma_format;
            info[i].max_width = surface->max_width;
            info[i].max_height = surface->max_height;
            info[i].subpicture_max_width = surface->subpicture_max_width;
            info[i].subpicture_max_height = surface->subpicture_max_height;
            info[i].mc_type = surface->mc_type;
            info[i].flags = surface->flags;
        }
    }

    xvmcListSurfaceTypesReply rep = {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .num = num_surfaces,
        .length = bytes_to_int32(sizeof(xvmcSurfaceInfo) * num_surfaces),
    };

    WriteToClient(client, sizeof(xvmcListSurfaceTypesReply), &rep);
    WriteToClient(client, sizeof(xvmcSurfaceInfo) * num_surfaces, info);
    free(info);

    return Success;
}

static int
ProcXvMCCreateContext(ClientPtr client)
{
    XvPortPtr pPort;
    CARD32 *data = NULL;
    int dwords = 0;
    int result, adapt_num = -1;
    ScreenPtr pScreen;
    XvMCContextPtr pContext;
    XvMCScreenPtr pScreenPriv;
    XvMCAdaptorPtr adaptor = NULL;
    XvMCSurfaceInfoPtr surface = NULL;

    REQUEST_HEAD_STRUCT(xvmcCreateContextReq);
    REQUEST_FIELD_CARD32(context_id);
    REQUEST_FIELD_CARD16(width);
    REQUEST_FIELD_CARD16(height);
    REQUEST_FIELD_CARD32(flags);

    VALIDATE_XV_PORT(stuff->port, pPort, DixReadAccess);

    pScreen = pPort->pAdaptor->pScreen;

    if (!XvMCInUse)             /* no XvMC adaptors */
        return BadMatch;

    if (!(pScreenPriv = XVMC_GET_PRIVATE(pScreen)))     /* none this screen */
        return BadMatch;

    for (int i = 0; i < pScreenPriv->num_adaptors; i++) {
        if (pPort->pAdaptor == pScreenPriv->adaptors[i].xv_adaptor) {
            adaptor = &(pScreenPriv->adaptors[i]);
            adapt_num = i;
            break;
        }
    }

    if (adapt_num < 0)          /* none this port */
        return BadMatch;

    for (int i = 0; i < adaptor->num_surfaces; i++) {
        if (adaptor->surfaces[i]->surface_type_id == stuff->surface_type_id) {
            surface = adaptor->surfaces[i];
            break;
        }
    }

    /* adaptor doesn't support this suface_type_id */
    if (!surface)
        return BadMatch;

    if ((stuff->width > surface->max_width) ||
        (stuff->height > surface->max_height))
        return BadValue;

    if (!(pContext = calloc(1, sizeof(XvMCContextRec)))) {
        return BadAlloc;
    }

    pContext->pScreen = pScreen;
    pContext->adapt_num = adapt_num;
    pContext->context_id = stuff->context_id;
    pContext->surface_type_id = stuff->surface_type_id;
    pContext->width = stuff->width;
    pContext->height = stuff->height;
    pContext->flags = stuff->flags;
    pContext->refcnt = 1;

    result = (*adaptor->CreateContext) (pPort, pContext, &dwords, &data);

    if (result != Success) {
        free(pContext);
        return result;
    }
    if (!AddResource(pContext->context_id, XvMCRTContext, pContext)) {
        free(data);
        return BadAlloc;
    }

    xvmcCreateContextReply rep = {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = dwords,
        .width_actual = pContext->width,
        .height_actual = pContext->height,
        .flags_return = pContext->flags
    };

    WriteToClient(client, sizeof(xvmcCreateContextReply), &rep);
    if (dwords)
        WriteToClient(client, dwords << 2, data);

    free(data);

    return Success;
}

static int
ProcXvMCDestroyContext(ClientPtr client)
{
    REQUEST_HEAD_STRUCT(xvmcDestroyContextReq);
    REQUEST_FIELD_CARD32(context_id);

    void *val;
    int rc;

    rc = dixLookupResourceByType(&val, stuff->context_id, XvMCRTContext,
                                 client, DixDestroyAccess);
    if (rc != Success)
        return rc;

    FreeResource(stuff->context_id, X11_RESTYPE_NONE);

    return Success;
}

static int
ProcXvMCCreateSurface(ClientPtr client)
{
    REQUEST_HEAD_STRUCT(xvmcCreateSurfaceReq);
    REQUEST_FIELD_CARD32(surface_id);
    REQUEST_FIELD_CARD32(context_id);

    CARD32 *data = NULL;
    int dwords = 0;
    int result;
    XvMCContextPtr pContext;
    XvMCSurfacePtr pSurface;
    XvMCScreenPtr pScreenPriv;

    result = dixLookupResourceByType((void **) &pContext, stuff->context_id,
                                     XvMCRTContext, client, DixUseAccess);
    if (result != Success)
        return result;

    pScreenPriv = XVMC_GET_PRIVATE(pContext->pScreen);

    if (!(pSurface = calloc(1, sizeof(XvMCSurfaceRec))))
        return BadAlloc;

    pSurface->surface_id = stuff->surface_id;
    pSurface->surface_type_id = pContext->surface_type_id;
    pSurface->context = pContext;

    result =
        (*pScreenPriv->adaptors[pContext->adapt_num].CreateSurface) (pSurface,
                                                                     &dwords,
                                                                     &data);

    if (result != Success) {
        free(pSurface);
        return result;
    }
    if (!AddResource(pSurface->surface_id, XvMCRTSurface, pSurface)) {
        free(data);
        return BadAlloc;
    }

    xvmcCreateSurfaceReply rep = {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = dwords
    };

    WriteToClient(client, sizeof(xvmcCreateSurfaceReply), &rep);
    if (dwords)
        WriteToClient(client, dwords << 2, data);

    free(data);

    pContext->refcnt++;

    return Success;
}

static int
ProcXvMCDestroySurface(ClientPtr client)
{
    REQUEST_HEAD_STRUCT(xvmcDestroySurfaceReq);
    REQUEST_FIELD_CARD32(surface_id);

    void *val;
    int rc;

    rc = dixLookupResourceByType(&val, stuff->surface_id, XvMCRTSurface,
                                 client, DixDestroyAccess);
    if (rc != Success)
        return rc;

    FreeResource(stuff->surface_id, X11_RESTYPE_NONE);

    return Success;
}

static int
ProcXvMCCreateSubpicture(ClientPtr client)
{
    REQUEST_HEAD_STRUCT(xvmcCreateSubpictureReq);
    REQUEST_FIELD_CARD32(subpicture_id);
    REQUEST_FIELD_CARD32(context_id);
    REQUEST_FIELD_CARD32(xvimage_id);
    REQUEST_FIELD_CARD16(width);
    REQUEST_FIELD_CARD16(height);

    Bool image_supported = FALSE;
    CARD32 *data = NULL;
    int result, dwords = 0;
    XvMCContextPtr pContext;
    XvMCSubpicturePtr pSubpicture;
    XvMCScreenPtr pScreenPriv;
    XvMCAdaptorPtr adaptor;
    XvMCSurfaceInfoPtr surface = NULL;

    result = dixLookupResourceByType((void **) &pContext, stuff->context_id,
                                     XvMCRTContext, client, DixUseAccess);
    if (result != Success)
        return result;

    pScreenPriv = XVMC_GET_PRIVATE(pContext->pScreen);

    adaptor = &(pScreenPriv->adaptors[pContext->adapt_num]);

    /* find which surface this context supports */
    for (int i = 0; i < adaptor->num_surfaces; i++) {
        if (adaptor->surfaces[i]->surface_type_id == pContext->surface_type_id) {
            surface = adaptor->surfaces[i];
            break;
        }
    }

    if (!surface)
        return BadMatch;

    /* make sure this surface supports that xvimage format */
    if (!surface->compatible_subpictures)
        return BadMatch;

    for (int i = 0; i < surface->compatible_subpictures->num_xvimages; i++) {
        if (surface->compatible_subpictures->xvimage_ids[i] ==
            stuff->xvimage_id) {
            image_supported = TRUE;
            break;
        }
    }

    if (!image_supported)
        return BadMatch;

    /* make sure the size is OK */
    if ((stuff->width > surface->subpicture_max_width) ||
        (stuff->height > surface->subpicture_max_height))
        return BadValue;

    if (!(pSubpicture = calloc(1, sizeof(XvMCSubpictureRec))))
        return BadAlloc;

    pSubpicture->subpicture_id = stuff->subpicture_id;
    pSubpicture->xvimage_id = stuff->xvimage_id;
    pSubpicture->width = stuff->width;
    pSubpicture->height = stuff->height;
    pSubpicture->num_palette_entries = 0;       /* overwritten by DDX */
    pSubpicture->entry_bytes = 0;       /* overwritten by DDX */
    pSubpicture->component_order[0] = 0;        /* overwritten by DDX */
    pSubpicture->component_order[1] = 0;
    pSubpicture->component_order[2] = 0;
    pSubpicture->component_order[3] = 0;
    pSubpicture->context = pContext;

    result =
        (*pScreenPriv->adaptors[pContext->adapt_num].
         CreateSubpicture) (pSubpicture, &dwords, &data);

    if (result != Success) {
        free(pSubpicture);
        return result;
    }
    if (!AddResource(pSubpicture->subpicture_id, XvMCRTSubpicture, pSubpicture)) {
        free(data);
        return BadAlloc;
    }

    xvmcCreateSubpictureReply rep = {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = dwords,
        .width_actual = pSubpicture->width,
        .height_actual = pSubpicture->height,
        .num_palette_entries = pSubpicture->num_palette_entries,
        .entry_bytes = pSubpicture->entry_bytes,
        .component_order[0] = pSubpicture->component_order[0],
        .component_order[1] = pSubpicture->component_order[1],
        .component_order[2] = pSubpicture->component_order[2],
        .component_order[3] = pSubpicture->component_order[3]
    };

    WriteToClient(client, sizeof(xvmcCreateSubpictureReply), &rep);
    if (dwords)
        WriteToClient(client, dwords << 2, data);

    free(data);

    pContext->refcnt++;

    return Success;
}

static int
ProcXvMCDestroySubpicture(ClientPtr client)
{
    REQUEST_HEAD_STRUCT(xvmcDestroySubpictureReq);
    REQUEST_FIELD_CARD32(subpicture_id);

    void *val;
    int rc;

    rc = dixLookupResourceByType(&val, stuff->subpicture_id, XvMCRTSubpicture,
                                 client, DixDestroyAccess);
    if (rc != Success)
        return rc;

    FreeResource(stuff->subpicture_id, X11_RESTYPE_NONE);

    return Success;
}

static int
ProcXvMCListSubpictureTypes(ClientPtr client)
{
    REQUEST_HEAD_STRUCT(xvmcListSubpictureTypesReq);
    REQUEST_FIELD_CARD32(port);
    REQUEST_FIELD_CARD32(surface_type_id);

    XvPortPtr pPort;
    XvMCScreenPtr pScreenPriv;
    ScreenPtr pScreen;
    XvMCAdaptorPtr adaptor = NULL;
    XvMCSurfaceInfoPtr surface = NULL;
    XvImagePtr pImage;

    VALIDATE_XV_PORT(stuff->port, pPort, DixReadAccess);

    pScreen = pPort->pAdaptor->pScreen;

    if (!dixPrivateKeyRegistered(XvMCScreenKey))
        return BadMatch;        /* No XvMC adaptors */

    if (!(pScreenPriv = XVMC_GET_PRIVATE(pScreen)))
        return BadMatch;        /* None this screen */

    for (int i = 0; i < pScreenPriv->num_adaptors; i++) {
        if (pPort->pAdaptor == pScreenPriv->adaptors[i].xv_adaptor) {
            adaptor = &(pScreenPriv->adaptors[i]);
            break;
        }
    }

    if (!adaptor)
        return BadMatch;

    for (int i = 0; i < adaptor->num_surfaces; i++) {
        if (adaptor->surfaces[i]->surface_type_id == stuff->surface_type_id) {
            surface = adaptor->surfaces[i];
            break;
        }
    }

    if (!surface)
        return BadMatch;

    int num = (surface->compatible_subpictures ?
               surface->compatible_subpictures->num_xvimages : 0);

    xvImageFormatInfo *info = NULL;
    if (num) {
        info = calloc(sizeof(xvImageFormatInfo), num);
        if (!info)
            return BadAlloc;

        for (int i = 0; i < num; i++) {
            pImage = NULL;
            for (int j = 0; j < adaptor->num_subpictures; j++) {
                if (surface->compatible_subpictures->xvimage_ids[i] ==
                    adaptor->subpictures[j]->id) {
                    pImage = adaptor->subpictures[j];
                    break;
                }
            }
            if (!pImage) {
                free(info);
                return BadImplementation;
            }

            info[i].id = pImage->id;
            info[i].type = pImage->type;
            info[i].byte_order = pImage->byte_order;
            memcpy(&info[i].guid, pImage->guid, 16);
            info[i].bpp = pImage->bits_per_pixel;
            info[i].num_planes = pImage->num_planes;
            info[i].depth = pImage->depth;
            info[i].red_mask = pImage->red_mask;
            info[i].green_mask = pImage->green_mask;
            info[i].blue_mask = pImage->blue_mask;
            info[i].format = pImage->format;
            info[i].y_sample_bits = pImage->y_sample_bits;
            info[i].u_sample_bits = pImage->u_sample_bits;
            info[i].v_sample_bits = pImage->v_sample_bits;
            info[i].horz_y_period = pImage->horz_y_period;
            info[i].horz_u_period = pImage->horz_u_period;
            info[i].horz_v_period = pImage->horz_v_period;
            info[i].vert_y_period = pImage->vert_y_period;
            info[i].vert_u_period = pImage->vert_u_period;
            info[i].vert_v_period = pImage->vert_v_period;
            memcpy(&info[i].comp_order, pImage->component_order, 32);
            info[i].scanline_order = pImage->scanline_order;
        }
    }

    xvmcListSubpictureTypesReply rep = {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .num = num,
        .length = bytes_to_int32(num * sizeof(xvImageFormatInfo)),
    };

    WriteToClient(client, sizeof(xvmcListSubpictureTypesReply), &rep);
    WriteToClient(client, sizeof(xvImageFormatInfo) * num, info);
    free(info);
    return Success;
}

static int
ProcXvMCGetDRInfo(ClientPtr client)
{
    REQUEST_HEAD_STRUCT(xvmcGetDRInfoReq);
    REQUEST_FIELD_CARD32(port);
    REQUEST_FIELD_CARD32(shmKey);
    REQUEST_FIELD_CARD32(magic);

    XvPortPtr pPort;
    ScreenPtr pScreen;
    XvMCScreenPtr pScreenPriv;

#ifdef HAS_XVMCSHM
    volatile CARD32 *patternP;
#endif

    VALIDATE_XV_PORT(stuff->port, pPort, DixReadAccess);

    pScreen = pPort->pAdaptor->pScreen;
    pScreenPriv = XVMC_GET_PRIVATE(pScreen);

    int nameLen = strlen(pScreenPriv->clientDriverName) + 1;
    int busIDLen = strlen(pScreenPriv->busID) + 1;

    // buffer holds two zero-terminated strings, padded to 4-byte ints
    const size_t buflen = pad_to_int32(nameLen+busIDLen);
    char *buf = calloc(1, buflen);
    if (!buf)
        return BadAlloc;

    memcpy(buf, pScreenPriv->clientDriverName, nameLen);
    memcpy(buf+nameLen, pScreenPriv->busID, busIDLen);

    xvmcGetDRInfoReply rep = {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .major = pScreenPriv->major,
        .minor = pScreenPriv->minor,
        .patchLevel = pScreenPriv->patchLevel,
        .nameLen = nameLen,
        .busIDLen = busIDLen,
        .length = bytes_to_int32(sizeof(buf)),
        .isLocal = 1
    };

    /*
     * Read back to the client what she has put in the shared memory
     * segment she prepared for us.
     */

#ifdef HAS_XVMCSHM
    patternP = (CARD32 *) shmat(stuff->shmKey, NULL, SHM_RDONLY);
    if (-1 != (long) patternP) {
        volatile CARD32 *patternC = patternP;
        int i;
        CARD32 magic = stuff->magic;

        rep.isLocal = 1;
        i = 1024 / sizeof(CARD32);

        while (i--) {
            if (*patternC++ != magic) {
                rep.isLocal = 0;
                break;
            }
            magic = ~magic;
        }
        shmdt((char *) patternP);
    }
#endif                          /* HAS_XVMCSHM */

    WriteToClient(client, sizeof(xvmcGetDRInfoReply), &rep);
    WriteToClient(client, buflen, buf);
    free(buf);
    return Success;
}

static int
ProcXvMCDispatch(ClientPtr client)
{
    REQUEST(xReq);
    switch (stuff->data)
    {
        case xvmc_QueryVersion:
            return ProcXvMCQueryVersion(client);
        case xvmc_ListSurfaceTypes:
            return ProcXvMCListSurfaceTypes(client);
        case xvmc_CreateContext:
            return ProcXvMCCreateContext(client);
        case xvmc_DestroyContext:
            return ProcXvMCDestroyContext(client);
        case xvmc_CreateSurface:
            return ProcXvMCCreateSurface(client);
        case xvmc_DestroySurface:
            return ProcXvMCDestroySurface(client);
        case xvmc_CreateSubpicture:
            return ProcXvMCCreateSubpicture(client);
        case xvmc_DestroySubpicture:
            return ProcXvMCDestroySubpicture(client);
        case xvmc_ListSubpictureTypes:
            return ProcXvMCListSubpictureTypes(client);
        case xvmc_GetDRInfo:
            return ProcXvMCGetDRInfo(client);
        default:
            return BadRequest;
    }
}

static int _X_COLD
SProcXvMCDispatch(ClientPtr client)
{
    /* We only support local */
    return BadImplementation;
}

void
XvMCExtensionInit(void)
{
    ExtensionEntry *extEntry;

    if (!dixPrivateKeyRegistered(XvMCScreenKey))
        return;

    if (!(XvMCRTContext = CreateNewResourceType(XvMCDestroyContextRes,
                                                "XvMCRTContext")))
        return;

    if (!(XvMCRTSurface = CreateNewResourceType(XvMCDestroySurfaceRes,
                                                "XvMCRTSurface")))
        return;

    if (!(XvMCRTSubpicture = CreateNewResourceType(XvMCDestroySubpictureRes,
                                                   "XvMCRTSubpicture")))
        return;

    extEntry = AddExtension(XvMCName, XvMCNumEvents, XvMCNumErrors,
                            ProcXvMCDispatch, SProcXvMCDispatch,
                            NULL, StandardMinorOpcode);

    if (!extEntry)
        return;

    XvMCReqCode = extEntry->base;
    XvMCEventBase = extEntry->eventBase;
    SetResourceTypeErrorValue(XvMCRTContext,
                              extEntry->errorBase + XvMCBadContext);
    SetResourceTypeErrorValue(XvMCRTSurface,
                              extEntry->errorBase + XvMCBadSurface);
    SetResourceTypeErrorValue(XvMCRTSubpicture,
                              extEntry->errorBase + XvMCBadSubpicture);
}

static void XvMCScreenClose(CallbackListPtr *pcbl, ScreenPtr pScreen, void *unused)
{
    XvMCScreenPtr pScreenPriv = XVMC_GET_PRIVATE(pScreen);
    free(pScreenPriv);
    dixSetPrivate(&pScreen->devPrivates, XvMCScreenKey, NULL);
    dixScreenUnhookClose(pScreen, XvMCScreenClose);
}

int
XvMCScreenInit(ScreenPtr pScreen, int num, XvMCAdaptorPtr pAdapt)
{
    XvMCScreenPtr pScreenPriv;

    if (!dixRegisterPrivateKey(&XvMCScreenKeyRec, PRIVATE_SCREEN, 0))
        return BadAlloc;

    if (!(pScreenPriv = calloc(1, sizeof(XvMCScreenRec))))
        return BadAlloc;

    dixSetPrivate(&pScreen->devPrivates, XvMCScreenKey, pScreenPriv);

    dixScreenHookClose(pScreen, XvMCScreenClose);

    pScreenPriv->num_adaptors = num;
    pScreenPriv->adaptors = pAdapt;
    pScreenPriv->clientDriverName[0] = 0;
    pScreenPriv->busID[0] = 0;
    pScreenPriv->major = 0;
    pScreenPriv->minor = 0;
    pScreenPriv->patchLevel = 0;

    XvMCInUse = TRUE;

    return Success;
}

XvImagePtr
XvMCFindXvImage(XvPortPtr pPort, CARD32 id)
{
    XvImagePtr pImage = NULL;
    ScreenPtr pScreen = pPort->pAdaptor->pScreen;
    XvMCScreenPtr pScreenPriv;
    XvMCAdaptorPtr adaptor = NULL;

    if (!dixPrivateKeyRegistered(XvMCScreenKey))
        return NULL;

    if (!(pScreenPriv = XVMC_GET_PRIVATE(pScreen)))
        return NULL;

    for (int i = 0; i < pScreenPriv->num_adaptors; i++) {
        if (pPort->pAdaptor == pScreenPriv->adaptors[i].xv_adaptor) {
            adaptor = &(pScreenPriv->adaptors[i]);
            break;
        }
    }

    if (!adaptor)
        return NULL;

    for (int i = 0; i < adaptor->num_subpictures; i++) {
        if (adaptor->subpictures[i]->id == id) {
            pImage = adaptor->subpictures[i];
            break;
        }
    }

    return pImage;
}

int
xf86XvMCRegisterDRInfo(ScreenPtr pScreen, const char *name,
                       const char *busID, int major, int minor, int patchLevel)
{
    XvMCScreenPtr pScreenPriv = XVMC_GET_PRIVATE(pScreen);

    strlcpy(pScreenPriv->clientDriverName, name, DR_CLIENT_DRIVER_NAME_SIZE);
    strlcpy(pScreenPriv->busID, busID, DR_BUSID_SIZE);
    pScreenPriv->major = major;
    pScreenPriv->minor = minor;
    pScreenPriv->patchLevel = patchLevel;
    return Success;
}
