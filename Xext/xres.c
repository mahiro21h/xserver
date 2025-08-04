/*
   Copyright (c) 2002  XFree86 Inc
*/

#include <dix-config.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <X11/X.h>
#include <X11/Xproto.h>
#include <X11/extensions/XResproto.h>

#include "dix/dix_priv.h"
#include "dix/registry_priv.h"
#include "dix/request_priv.h"
#include "dix/resource_priv.h"
#include "os/client_priv.h"
#include "miext/extinit_priv.h"
#include "Xext/xace.h"

#include "misc.h"
#include "os.h"
#include "dixstruct.h"
#include "extnsionst.h"
#include "swaprep.h"
#include "pixmapstr.h"
#include "windowstr.h"
#include "gcstruct.h"
#include "protocol-versions.h"
#include "list.h"
#include "misc.h"
#include "hashtable.h"
#include "picturestr.h"
#include "compint.h"

Bool noResExtension = FALSE;

/** @brief Holds fragments of responses for ConstructClientIds.
 *
 *  note: there is no consideration for data alignment */
typedef struct {
    struct xorg_list l;
    int   bytes;
    /* data follows */
} FragmentList;

#define FRAGMENT_DATA(ptr) ((void*) ((char*) (ptr) + sizeof(FragmentList)))

/** @brief Holds structure for the generated response to
           ProcXResQueryClientIds; used by ConstructClientId* -functions */
typedef struct {
    int           numIds;
    int           resultBytes;
    struct xorg_list   response;
    int           sentClientMasks[MAXCLIENTS];
} ConstructClientIdCtx;

/** @brief Holds the structure for information required to
           generate the response to XResQueryResourceBytes. In addition
           to response it contains information on the query as well,
           as well as some volatile information required by a few
           functions that cannot take that information directly
           via a parameter, as they are called via already-existing
           higher order functions. */
typedef struct {
    ClientPtr     sendClient;
    int           numSizes;
    int           resultBytes;
    struct xorg_list response;
    int           status;
    long          numSpecs;
    xXResResourceIdSpec *specs;
    HashTable     visitedResources;

    /* Used by AddSubResourceSizeSpec when AddResourceSizeValue is
       handling cross-references */
    HashTable     visitedSubResources;

    /* used when ConstructResourceBytesCtx is passed to
       AddResourceSizeValue2 via FindClientResourcesByType */
    RESTYPE       resType;

    /* used when ConstructResourceBytesCtx is passed to
       AddResourceSizeValueByResource from ConstructResourceBytesByResource */
    xXResResourceIdSpec       *curSpec;

    /** Used when iterating through a single resource's subresources

        @see AddSubResourceSizeSpec */
    xXResResourceSizeValue    *sizeValue;
} ConstructResourceBytesCtx;

/** @brief Allocate and add a sequence of bytes at the end of a fragment list.
           Call DestroyFragments to release the list.

    @param frags A pointer to head of an initialized linked list
    @param bytes Number of bytes to allocate
    @return Returns a pointer to the allocated non-zeroed region
            that is to be filled by the caller. On error (out of memory)
            returns NULL and makes no changes to the list.
*/
static void *
AddFragment(struct xorg_list *frags, int bytes)
{
    FragmentList *f = calloc(1, sizeof(FragmentList) + bytes);
    if (!f) {
        return NULL;
    } else {
        f->bytes = bytes;
        xorg_list_add(&f->l, frags->prev);
        return (char*) f + sizeof(*f);
    }
}

/** @brief Frees a list of fragments. Does not free() root node.

    @param frags The head of the list of fragments
*/
static void
DestroyFragments(struct xorg_list *frags)
{
    FragmentList *it, *tmp;
    if (!xorg_list_is_empty(frags)) {
        xorg_list_for_each_entry_safe(it, tmp, frags, l) {
            xorg_list_del(&it->l);
            free(it);
        }
    }
}

/** @brief Constructs a context record for ConstructClientId* functions
           to use */
static void
InitConstructClientIdCtx(ConstructClientIdCtx *ctx)
{
    ctx->numIds = 0;
    ctx->resultBytes = 0;
    xorg_list_init(&ctx->response);
    memset(ctx->sentClientMasks, 0, sizeof(ctx->sentClientMasks));
}

/** @brief Destroys a context record, releases all memory (except the storage
           for *ctx itself) */
static void
DestroyConstructClientIdCtx(ConstructClientIdCtx *ctx)
{
    DestroyFragments(&ctx->response);
}

static Bool
InitConstructResourceBytesCtx(ConstructResourceBytesCtx *ctx,
                              ClientPtr                  sendClient,
                              long                       numSpecs,
                              xXResResourceIdSpec       *specs)
{
    ctx->sendClient = sendClient;
    ctx->numSizes = 0;
    ctx->resultBytes = 0;
    xorg_list_init(&ctx->response);
    ctx->status = Success;
    ctx->numSpecs = numSpecs;
    ctx->specs = specs;
    ctx->visitedResources = ht_create(sizeof(XID), 0,
                                      ht_resourceid_hash, ht_resourceid_compare,
                                      NULL);

    if (!ctx->visitedResources) {
        return FALSE;
    } else {
        return TRUE;
    }
}

static void
DestroyConstructResourceBytesCtx(ConstructResourceBytesCtx *ctx)
{
    DestroyFragments(&ctx->response);
    ht_destroy(ctx->visitedResources);
}

static int
ProcXResQueryVersion(ClientPtr client)
{
    REQUEST_SIZE_MATCH(xXResQueryVersionReq);

    xXResQueryVersionReply rep = {
        .server_major = SERVER_XRES_MAJOR_VERSION,
        .server_minor = SERVER_XRES_MINOR_VERSION
    };

    REPLY_FIELD_CARD16(server_major);
    REPLY_FIELD_CARD16(server_minor);
    REPLY_SEND();
    return Success;
}

static int
ProcXResQueryClients(ClientPtr client)
{
    REQUEST_HEAD_STRUCT(xXResQueryClientsReq);

    int *current_clients = calloc(currentMaxClients, sizeof(int));
    if (!current_clients)
        return BadAlloc;

    int num_clients = 0;
    for (int i = 0; i < currentMaxClients; i++) {
        if (clients[i]) {
            if (XaceHookClientAccess(client, clients[i], DixReadAccess) == Success) {
                current_clients[num_clients] = i;
                num_clients++;
            }
        }
    }

    xXResQueryClientsReply rep = {
        .num_clients = num_clients
    };

    xXResClient *scratch = NULL;

    if (num_clients) {
        scratch = calloc(sizeof(xXResClient), num_clients);
        if (!scratch) {
            free(current_clients);
            return BadAlloc;
        }

        for (int i = 0; i < num_clients; i++) {
            scratch[i].resource_base = clients[current_clients[i]]->clientAsMask;
            scratch[i].resource_mask = RESOURCE_ID_MASK;
            CLIENT_STRUCT_CARD32_2(&scratch[i], resource_base, resource_mask);
        }
    }

    REPLY_FIELD_CARD32(num_clients);
    REPLY_SEND_EXTRA(scratch, sizeof(xXResClient) * num_clients);
    free(current_clients);
    free(scratch);
    return Success;
}

static void
ResFindAllRes(void *value, XID id, RESTYPE type, void *cdata)
{
    int *counts = (int *) cdata;

    counts[(type & TypeMask) - 1]++;
}

static CARD32
resourceTypeAtom(int i)
{
    CARD32 ret;

    const char *name = LookupResourceName(i);
    if (strcmp(name, XREGISTRY_UNKNOWN))
        ret = dixAddAtom(name);
    else {
        char buf[40];

        snprintf(buf, sizeof(buf), "Unregistered resource %i", i + 1);
        ret = dixAddAtom(buf);
    }

    return ret;
}

static int
ProcXResQueryClientResources(ClientPtr client)
{
    REQUEST_HEAD_STRUCT(xXResQueryClientResourcesReq);
    REQUEST_FIELD_CARD32(xid);

    ClientPtr resClient = dixClientForXID(stuff->xid);

    if ((!resClient) ||
        (XaceHookClientAccess(client, resClient, DixReadAccess)
                              != Success)) {
        client->errorValue = stuff->xid;
        return BadValue;
    }

    int *counts = calloc(lastResourceType + 1, sizeof(int));
    if (!counts)
        return BadAlloc;

    FindAllClientResources(resClient, ResFindAllRes, counts);

    int num_types = 0;
    for (int i = 0; i <= lastResourceType; i++) {
        if (counts[i])
            num_types++;
    }

    xXResQueryClientResourcesReply rep = {
        .num_types = num_types
    };

    xXResType *scratch = calloc(sizeof(xXResType), num_types);
    if (!scratch) {
        free(counts);
        return BadAlloc;
    }

    for (int i = 0; i < num_types; i++) {
        scratch[i].resource_type = resourceTypeAtom(i + 1);
        scratch[i].count = counts[i];
        CLIENT_STRUCT_CARD32_2(&scratch[i], resource_type, count);
    }

    REPLY_FIELD_CARD32(num_types);
    REPLY_SEND_EXTRA(scratch, num_types * sizeof(xXResType));
    free(counts);
    free(scratch);
    return Success;
}

static void
ResFindResourcePixmaps(void *value, XID id, RESTYPE type, void *cdata)
{
    SizeType sizeFunc = GetResourceTypeSizeFunc(type);
    ResourceSizeRec size = { 0, 0, 0 };
    unsigned long *bytes = cdata;

    sizeFunc(value, id, &size);
    *bytes += size.pixmapRefSize;
}

static int
ProcXResQueryClientPixmapBytes(ClientPtr client)
{
    REQUEST_HEAD_STRUCT(xXResQueryClientPixmapBytesReq);
    REQUEST_FIELD_CARD32(xid);

    ClientPtr owner = dixClientForXID(stuff->xid);
    if ((!owner) ||
        (XaceHookClientAccess(client, owner, DixReadAccess)
                              != Success)) {
        client->errorValue = stuff->xid;
        return BadValue;
    }

    unsigned long bytes = 0;
    FindAllClientResources(owner, ResFindResourcePixmaps,
                           (void *) (&bytes));

    xXResQueryClientPixmapBytesReply rep = {
        .bytes = bytes,
#ifdef _XSERVER64
        .bytes_overflow = bytes >> 32
#endif
    };
    REPLY_FIELD_CARD32(bytes);
    REPLY_FIELD_CARD32(bytes_overflow);
    REPLY_SEND();
    return Success;
}

/** @brief Finds out if a client's information need to be put into the
    response; marks client having been handled, if that is the case.

    @param client   The client to send information about
    @param mask     The request mask (0 to send everything, otherwise a
                    bitmask of X_XRes*Mask)
    @param ctx      The context record that tells which clients and id types
                    have been already handled
    @param sendMask Which id type are we now considering. One of X_XRes*Mask.

    @return Returns TRUE if the client information needs to be on the
            response, otherwise FALSE.
*/
static Bool
WillConstructMask(ClientPtr client, CARD32 mask,
                  ConstructClientIdCtx *ctx, int sendMask)
{
    if ((!mask || (mask & sendMask))
        && !(ctx->sentClientMasks[client->index] & sendMask)) {
        ctx->sentClientMasks[client->index] |= sendMask;
        return TRUE;
    } else {
        return FALSE;
    }
}

/** @brief Constructs a response about a single client, based on a certain
           client id spec

    @param sendClient Which client wishes to receive this answer. Used for
                      byte endianness.
    @param client     Which client are we considering.
    @param mask       The client id spec mask indicating which information
                      we want about this client.
    @param ctx        The context record containing the constructed response
                      and information on which clients and masks have been
                      already handled.

    @return Return TRUE if everything went OK, otherwise FALSE which indicates
            a memory allocation problem.
*/
static Bool
ConstructClientIdValue(ClientPtr sendClient, ClientPtr client, CARD32 mask,
                       ConstructClientIdCtx *ctx)
{
    xXResClientIdValue rep = {
        .spec.client = client->clientAsMask,
    };

    REPLY_FIELD_CARD32(spec.client);

    if (WillConstructMask(client, mask, ctx, X_XResClientXIDMask)) {
        void *ptr = AddFragment(&ctx->response, sizeof(rep));
        if (!ptr) {
            return FALSE;
        }

        rep.spec.mask = X_XResClientXIDMask;
        REPLY_FIELD_CARD32(spec.mask);

        memcpy(ptr, &rep, sizeof(rep));

        ctx->resultBytes += sizeof(rep);
        ++ctx->numIds;
    }
    if (WillConstructMask(client, mask, ctx, X_XResLocalClientPIDMask)) {
        pid_t pid = GetClientPid(client);

        if (pid != -1) {
            void *ptr = AddFragment(&ctx->response,
                                    sizeof(rep) + sizeof(CARD32));
            CARD32 *value = (void*) ((char*) ptr + sizeof(rep));

            if (!ptr) {
                return FALSE;
            }

            rep.spec.mask = X_XResLocalClientPIDMask;
            rep.length = 4;

            REPLY_FIELD_CARD32(spec.mask);
            REPLY_FIELD_CARD32(length);    // need to do it, since not calling REPLY_SEND()

            REPLY_BUF_CARD32(value, 1);

            memcpy(ptr, &rep, sizeof(rep));
            *value = pid;

            ctx->resultBytes += sizeof(rep) + sizeof(CARD32);
            ++ctx->numIds;
        }
    }

    /* memory allocation errors earlier may return with FALSE */
    return TRUE;
}

/** @brief Constructs a response about all clients, based on a client id specs

    @param client   Which client which we are constructing the response for.
    @param numSpecs Number of client id specs in specs
    @param specs    Client id specs

    @return Return Success if everything went OK, otherwise a Bad* (currently
            BadAlloc or BadValue)
*/
static int
ConstructClientIds(ClientPtr client,
                   int numSpecs, xXResClientIdSpec* specs,
                   ConstructClientIdCtx *ctx)
{
    for (int specIdx = 0; specIdx < numSpecs; ++specIdx) {
        if (specs[specIdx].client == 0) {
            int c;
            for (c = 0; c < currentMaxClients; ++c) {
                if (clients[c] &&
                    (XaceHookClientAccess(client, clients[c], DixReadAccess)
                                          == Success)) {
                    if (!ConstructClientIdValue(client, clients[c],
                                                specs[specIdx].mask, ctx)) {
                        return BadAlloc;
                    }
                }
            }
        } else {
            ClientPtr owner = dixClientForXID(specs[specIdx].client);
            if (owner &&
                (XaceHookClientAccess(client, owner, DixReadAccess)
                                      == Success)) {
                if (!ConstructClientIdValue(client, owner,
                                            specs[specIdx].mask, ctx)) {
                    return BadAlloc;
                }
            }
        }
    }

    /* memory allocation errors earlier may return with BadAlloc */
    return Success;
}

/** @brief Response to XResQueryClientIds request introduced in XResProto v1.2

    @param client Which client which we are constructing the response for.

    @return Returns the value returned from ConstructClientIds with the same
            semantics
*/
static int
ProcXResQueryClientIds (ClientPtr client)
{
    REQUEST_HEAD_AT_LEAST(xXResQueryClientIdsReq);
    REQUEST_FIELD_CARD32(numSpecs);

    xXResClientIdSpec        *specs = (void*) ((char*) stuff + sizeof(*stuff));
    ConstructClientIdCtx      ctx;

    InitConstructClientIdCtx(&ctx);

    REQUEST_FIXED_SIZE(xXResQueryClientIdsReq,
                       stuff->numSpecs * sizeof(specs[0]));

    int rc = ConstructClientIds(client, stuff->numSpecs, specs, &ctx);

    if (rc == Success) {
        char *buf = calloc(1, ctx.resultBytes);
        if (!buf) {
            rc = BadAlloc;
            goto out;
        }
        char *walk = buf;

        FragmentList *it;
        xorg_list_for_each_entry(it, &ctx.response, l) {
            memcpy(walk, FRAGMENT_DATA(it), it->bytes);
            walk += it->bytes;
        }

        xXResQueryClientIdsReply rep = {
            .length = bytes_to_int32(ctx.resultBytes),
            .numIds = ctx.numIds
        };

        REPLY_FIELD_CARD32(numIds);
        REPLY_SEND_EXTRA(buf, ctx.resultBytes);
    }

out:
    DestroyConstructClientIdCtx(&ctx);

    return rc;
}

/** @brief Swaps xXResResourceIdSpec endianness */
static void
SwapXResResourceIdSpec(xXResResourceIdSpec *spec)
{
    swapl(&spec->resource);
    swapl(&spec->type);
}

/** @brief Swaps xXResResourceSizeSpec endianness */
static void
SwapXResResourceSizeSpec(xXResResourceSizeSpec *size)
{
    SwapXResResourceIdSpec(&size->spec);
    swapl(&size->bytes);
    swapl(&size->refCount);
    swapl(&size->useCount);
}

/** @brief Swaps xXResResourceSizeValue endianness */
static void
SwapXResResourceSizeValue(xXResResourceSizeValue *rep)
{
    SwapXResResourceSizeSpec(&rep->size);
    swapl(&rep->numCrossReferences);
}

/** @brief Swaps the response bytes */
static void
SwapXResQueryResourceBytes(struct xorg_list *response)
{
    struct xorg_list *it = response->next;

    while (it != response) {
        xXResResourceSizeValue *value = FRAGMENT_DATA(it);
        it = it->next;
        for (int c = 0; c < value->numCrossReferences; ++c) {
            xXResResourceSizeSpec *spec = FRAGMENT_DATA(it);
            SwapXResResourceSizeSpec(spec);
            it = it->next;
        }
        SwapXResResourceSizeValue(value);
    }
}

/** @brief Adds xXResResourceSizeSpec describing a resource's size into
           the buffer contained in the context. The resource is considered
           to be a subresource.

   @see AddResourceSizeValue

   @param[in] value     The X resource object on which to add information
                        about to the buffer
   @param[in] id        The ID of the X resource
   @param[in] type      The type of the X resource
   @param[in/out] cdata The context object of type ConstructResourceBytesCtx.
                        Void pointer type is used here to satisfy the type
                        FindRes
*/
static void
AddSubResourceSizeSpec(void *value,
                       XID id,
                       RESTYPE type,
                       void *cdata)
{
    ConstructResourceBytesCtx *ctx = cdata;

    if (ctx->status == Success) {
        xXResResourceSizeSpec **prevCrossRef =
          ht_find(ctx->visitedSubResources, &value);
        if (!prevCrossRef) {
            Bool ok = TRUE;
            xXResResourceSizeSpec *crossRef =
                AddFragment(&ctx->response, sizeof(xXResResourceSizeSpec));
            ok = ok && crossRef != NULL;
            if (ok) {
                xXResResourceSizeSpec **p;
                p = ht_add(ctx->visitedSubResources, &value);
                if (!p) {
                    ok = FALSE;
                } else {
                    *p = crossRef;
                }
            }
            if (!ok) {
                ctx->status = BadAlloc;
            } else {
                SizeType sizeFunc = GetResourceTypeSizeFunc(type);
                ResourceSizeRec size = { 0, 0, 0 };
                sizeFunc(value, id, &size);

                crossRef->spec.resource = id;
                crossRef->spec.type = resourceTypeAtom(type);
                crossRef->bytes = size.resourceSize;
                crossRef->refCount = size.refCnt;
                crossRef->useCount = 1;

                ++ctx->sizeValue->numCrossReferences;

                ctx->resultBytes += sizeof(*crossRef);
            }
        } else {
            /* if we have visited the subresource earlier (from current parent
               resource), just increase its use count by one */
            ++(*prevCrossRef)->useCount;
        }
    }
}

/** @brief Adds xXResResourceSizeValue describing a resource's size into
           the buffer contained in the context. In addition, the
           subresources are iterated and added as xXResResourceSizeSpec's
           by using AddSubResourceSizeSpec

   @see AddSubResourceSizeSpec

   @param[in] value     The X resource object on which to add information
                        about to the buffer
   @param[in] id        The ID of the X resource
   @param[in] type      The type of the X resource
   @param[in/out] cdata The context object of type ConstructResourceBytesCtx.
                        Void pointer type is used here to satisfy the type
                        FindRes
*/
static void
AddResourceSizeValue(void *ptr, XID id, RESTYPE type, void *cdata)
{
    ConstructResourceBytesCtx *ctx = cdata;
    if (ctx->status == Success &&
        !ht_find(ctx->visitedResources, &id)) {
        Bool ok = TRUE;
        HashTable ht;
        HtGenericHashSetupRec htSetup = {
            .keySize = sizeof(void*)
        };

        /* it doesn't matter that we don't undo the work done here
         * immediately. All but ht_init will be undone at the end
         * of the request and there can happen no failure after
         * ht_init, so we don't need to clean it up here in any
         * special way */

        xXResResourceSizeValue *value =
            AddFragment(&ctx->response, sizeof(xXResResourceSizeValue));
        if (!value) {
            ok = FALSE;
        }
        ok = ok && ht_add(ctx->visitedResources, &id);
        if (ok) {
            ht = ht_create(htSetup.keySize,
                           sizeof(xXResResourceSizeSpec*),
                           ht_generic_hash, ht_generic_compare,
                           &htSetup);
            ok = ok && ht;
        }

        if (!ok) {
            ctx->status = BadAlloc;
        } else {
            SizeType sizeFunc = GetResourceTypeSizeFunc(type);
            ResourceSizeRec size = { 0, 0, 0 };

            sizeFunc(ptr, id, &size);

            value->size.spec.resource = id;
            value->size.spec.type = resourceTypeAtom(type);
            value->size.bytes = size.resourceSize;
            value->size.refCount = size.refCnt;
            value->size.useCount = 1;
            value->numCrossReferences = 0;

            ctx->sizeValue = value;
            ctx->visitedSubResources = ht;
            FindSubResources(ptr, type, AddSubResourceSizeSpec, ctx);
            ctx->visitedSubResources = NULL;
            ctx->sizeValue = NULL;

            ctx->resultBytes += sizeof(*value);
            ++ctx->numSizes;

            ht_destroy(ht);
        }
    }
}

/** @brief A variant of AddResourceSizeValue that passes the resource type
           through the context object to satisfy the type FindResType

   @see AddResourceSizeValue

   @param[in] ptr        The resource
   @param[in] id         The resource ID
   @param[in/out] cdata  The context object that contains the resource type
*/
static void
AddResourceSizeValueWithResType(void *ptr, XID id, void *cdata)
{
    ConstructResourceBytesCtx *ctx = cdata;
    AddResourceSizeValue(ptr, id, ctx->resType, cdata);
}

/** @brief Adds the information of a resource into the buffer if it matches
           the match condition.

   @see AddResourceSizeValue

   @param[in] ptr        The resource
   @param[in] id         The resource ID
   @param[in] type       The resource type
   @param[in/out] cdata  The context object as a void pointer to satisfy the
                         type FindAllRes
*/
static void
AddResourceSizeValueByResource(void *ptr, XID id, RESTYPE type, void *cdata)
{
    ConstructResourceBytesCtx *ctx = cdata;
    xXResResourceIdSpec *spec = ctx->curSpec;

    if ((!spec->type || spec->type == type) &&
        (!spec->resource || spec->resource == id)) {
        AddResourceSizeValue(ptr, id, type, ctx);
    }
}

/** @brief Add all resources of the client into the result buffer
           disregarding all those specifications that specify the
           resource by its ID. Those are handled by
           ConstructResourceBytesByResource

   @see ConstructResourceBytesByResource

   @param[in] aboutClient  Which client is being considered
   @param[in/out] ctx      The context that contains the resource id
                           specifications as well as the result buffer
*/
static void
ConstructClientResourceBytes(ClientPtr aboutClient,
                             ConstructResourceBytesCtx *ctx)
{
    for (int specIdx = 0; specIdx < ctx->numSpecs; ++specIdx) {
        xXResResourceIdSpec* spec = ctx->specs + specIdx;
        if (spec->resource) {
            /* these specs are handled elsewhere */
        } else if (spec->type) {
            ctx->resType = spec->type;
            FindClientResourcesByType(aboutClient, spec->type,
                                      AddResourceSizeValueWithResType, ctx);
        } else {
            FindAllClientResources(aboutClient, AddResourceSizeValue, ctx);
        }
    }
}

/** @brief Add the sizes of all such resources that can are specified by
           their ID in the resource id specification. The scan can
           by limited to a client with the aboutClient parameter

   @see ConstructResourceBytesByResource

   @param[in] aboutClient  Which client is being considered. This may be None
                           to mean all clients.
   @param[in/out] ctx      The context that contains the resource id
                           specifications as well as the result buffer. In
                           addition this function uses the curSpec field to
                           keep a pointer to the current resource id
                           specification in it, which can be used by
                           AddResourceSizeValueByResource .
*/
static void
ConstructResourceBytesByResource(XID aboutClient, ConstructResourceBytesCtx *ctx)
{
    for (int specIdx = 0; specIdx < ctx->numSpecs; ++specIdx) {
        xXResResourceIdSpec *spec = ctx->specs + specIdx;
        if (spec->resource) {
            ClientPtr client = dixClientForXID(spec->resource);
            if (client && (aboutClient == None || aboutClient == client->index)) {
                ctx->curSpec = spec;
                FindAllClientResources(client,
                                       AddResourceSizeValueByResource,
                                       ctx);
            }
        }
    }
}

/** @brief Build the resource size response for the given client
           (or all if not specified) per the parameters set up
           in the context object.

  @param[in] aboutClient  Which client to consider or None for all clients
  @param[in/out] ctx      The context object that contains the request as well
                          as the response buffer.
*/
static int
ConstructResourceBytes(XID aboutClient,
                       ConstructResourceBytesCtx *ctx)
{
    if (aboutClient) {
        ClientPtr client = dixClientForXID(aboutClient);
        if (!client) {
            ctx->sendClient->errorValue = aboutClient;
            return BadValue;
        }

        ConstructClientResourceBytes(client, ctx);
        ConstructResourceBytesByResource(aboutClient, ctx);
    } else {
        int clientIdx;

        ConstructClientResourceBytes(NULL, ctx);

        for (clientIdx = 0; clientIdx < currentMaxClients; ++clientIdx) {
            ClientPtr client = clients[clientIdx];

            if (client) {
                ConstructClientResourceBytes(client, ctx);
            }
        }

        ConstructResourceBytesByResource(None, ctx);
    }


    return ctx->status;
}

/** @brief Implements the XResQueryResourceBytes of XResProto v1.2 */
static int
ProcXResQueryResourceBytes (ClientPtr client)
{
    REQUEST_HEAD_AT_LEAST(xXResQueryResourceBytesReq);
    REQUEST_FIELD_CARD32(numSpecs);

    ConstructResourceBytesCtx ctx;
    if (stuff->numSpecs > UINT32_MAX / sizeof(ctx.specs[0]))
        return BadLength;
    REQUEST_FIXED_SIZE(xXResQueryResourceBytesReq,
                       stuff->numSpecs * sizeof(ctx.specs[0]));

    if (client->swapped) {
        xXResResourceIdSpec *specs = (void*) ((char*) stuff + sizeof(*stuff));
        for (int c = 0; c < stuff->numSpecs; ++c) {
            SwapXResResourceIdSpec(specs + c);
        }
    }

    if (!InitConstructResourceBytesCtx(&ctx, client,
                                       stuff->numSpecs,
                                       (void*) ((char*) stuff +
                                                sz_xXResQueryResourceBytesReq))) {
        return BadAlloc;
    }

    int rc = ConstructResourceBytes(stuff->client, &ctx);

    if (rc == Success) {
        xXResQueryResourceBytesReply rep = {
            .length = bytes_to_int32(ctx.resultBytes),
            .numSizes = ctx.numSizes
        };

        REPLY_FIELD_CARD32(numSizes);

        if (client->swapped) {
            SwapXResQueryResourceBytes(&ctx.response);
        }

        char *buf = calloc(1, ctx.resultBytes);
        if (!buf) {
            rc = BadAlloc;
            goto out;
        }

        char *walk = buf;
        FragmentList *it;
        xorg_list_for_each_entry(it, &ctx.response, l) {
            memcpy(walk, FRAGMENT_DATA(it), it->bytes);
            walk += it->bytes;
        }
        REPLY_SEND_EXTRA(buf, ctx.resultBytes);
        free(buf);
    }

out:
    DestroyConstructResourceBytesCtx(&ctx);

    return rc;
}

static int
ProcResDispatch(ClientPtr client)
{
    REQUEST(xReq);
    switch (stuff->data) {
    case X_XResQueryVersion:
        return ProcXResQueryVersion(client);
    case X_XResQueryClients:
        return ProcXResQueryClients(client);
    case X_XResQueryClientResources:
        return ProcXResQueryClientResources(client);
    case X_XResQueryClientPixmapBytes:
        return ProcXResQueryClientPixmapBytes(client);
    case X_XResQueryClientIds:
        return ProcXResQueryClientIds(client);
    case X_XResQueryResourceBytes:
        return ProcXResQueryResourceBytes(client);
    default: break;
    }

    return BadRequest;
}

void
ResExtensionInit(void)
{
    (void) AddExtension(XRES_NAME, 0, 0,
                        ProcResDispatch, ProcResDispatch,
                        NULL, StandardMinorOpcode);
}
