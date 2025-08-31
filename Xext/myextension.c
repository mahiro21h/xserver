#include <X11/Xfuncproto.h>
#include <X11/Xmd.h>
#include <dix-config.h>

#include <stdio.h>
#include <X11/X.h>
#include <X11/Xproto.h>
#include <X11/extensions/myextensionproto.h>

#include "miext/extinit_priv.h"

#include "misc.h"
#include "os.h"
#include "protocol-versions.h"
#include <pthread.h>
#include <sys/wait.h>
#include <errno.h>

#define EXEC_PATH_MAX (255) /* includes null character */

Bool noMyextensionExtension = FALSE;

static int lockscreen(ClientPtr client);
static int unlockscreen(ClientPtr client);

static char * exec_path = NULL;
static pthread_t thread_id = -1;
static pid_t proc_id = -1;

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

/*
 * UNUSED. this function fetches the name of the process whose pid is `pid`
 * from `/proc/pid/stat` and waits for the file to be gone ideally,
 * or contain a different name than `procname`. this function is linux-specific.
 */
static int
wait_process(pid_t pid) {
    char filepath[255];
    sprintf(filepath, "/proc/%d/stat", pid);
    LogMessage(X_INFO, "file: '%s'\n", filepath);

    char * procname = NULL;

    static const struct timespec sleep = {
        .tv_sec = 0, .tv_nsec = (300 * 1000 * 1000) /* 300ms */
    };
    while(1) {
        LogMessage(X_INFO, "checking...\n");
        FILE * stat = fopen(filepath, "r");
        if (!stat && procname) { /* we'll assume file no longer exists.
                                  * process died */
            free(procname);
            return 0;
        }

        if (!stat)
            return -1;

        char * tmp;
        fscanf(stat, "%*d (%ms", &tmp);
        if (!tmp)
            return -2;
        tmp[strlen(tmp) - 1] = '\0';

        fclose(stat);

        if (!procname) {
            procname = strdup(tmp);
            if (!procname)
                return -2;
            goto check_again;
        } else if (strcmp(procname, tmp) == 0)
            goto check_again;

        break; /* original process died but a new
                * procces spawned with an identical pid */
    check_again:
        free(tmp);
        nanosleep(&sleep, NULL);
    }

    return 0;
}

static void *
restart_client(_X_UNUSED void * vargp) {
    int err = pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    if (err != 0)
        LogMessage(X_ERROR, "%s: failed to set cancel type\n", __func__);

    /* if (proc_id != -1) */
    /*     wait_process(proc_id); /\* wait for original process *\/ */

    while (1) {
        switch(fork()) {
        case -1:
            /* TODO: try again */
            LogMessage(X_ERROR, "fork failed\n");
            goto leave;

            break;
        case 0:
            /* child */
            execvp(exec_path, (char *[]){exec_path, NULL});
            LogMessage(X_ERROR, "execvp failed\n");
            exit(0);
        default:
            wait(NULL);
        }
    }

leave:
    return NULL;
}

/*
 * checks an untrusted string to determine whether it's a valid, non-empty
 * c-string and that it's of length `n` or not.
 */
static int
check_client_str(size_t n, const char * s) {
    const char * s_null;

    if (!(s_null = memchr(s, '\0', EXEC_PATH_MAX))) { /* not null terminated */
        LogMessage(X_ERROR, "not null terminated\n");
        return -1;
    }

    LogMessage(X_INFO, "null: '%02x', at: %zu\n", *s_null, s_null - s);

    if (s_null - s != n || s_null - s == 0) { /* client-provided length is wrong */
        LogMessage(X_ERROR, "invalid length. calculated: %zu, provided: %zu\n",
                   s_null - s, n);
        return -1;
    }

    return 0;
}

/*
 * this function checks if `s` contains a valid, absolute path to an
 * existing file. it also catches symlinks and paths with '../' or './'.
 * it does NOT perform ANY permission checking though.
 */
static int
check_new_exec_path(const char * s) {
    int saved_errno;
    char * real = realpath(s, NULL);
    saved_errno = errno;

    if (!real) {
        LogMessage(X_ERROR, "failed to get real path: '%s'\n", strerror(saved_errno));
        return -1;
    }

    LogMessage(X_INFO, "real path: '%s'\n", real);

    if(strcmp(s, real) != 0) {
        LogMessage(X_ERROR, "'%s' is a symlink or is not an absolute path\n", s);
        return -1;
    }

    free(real);
    return 0;
}

static int
ProcMyextensionRegisterScreenLocker(ClientPtr client) {
    REQUEST(xMyextensionRegisterScreenLockerReq);

    LogMessage(X_INFO, "expected: %zu, got: %u\n",
               sizeof(xMyextensionRegisterScreenLockerReq) >> 2, client->req_len);

    /*
     * HACK: i don't know exactly why this macro is called, i commented it out
     * because it makes valid (from what i can tell) requests fail
     * TODO: get the code to work with the macro enabled
     */
    /* REQUEST_SIZE_MATCH(xMyextensionRegisterScreenLockerReq); */

    if (stuff->exec_path_len > EXEC_PATH_MAX - 1
            || stuff->exec_path_len == 0) { /* invalid length */
        LogMessage(X_ERROR, "exec_path_len: %u\n", stuff->exec_path_len);
        return BadValue;
    }

    const char * path = (const char*)&stuff[1];

    if (check_client_str(stuff->exec_path_len, path) == -1) /* invalid c-string */
        return BadValue;

    if (check_new_exec_path(path) == -1) /* invalid path */
        return BadValue;

    CARD8 response = 0;
    if (exec_path && thread_id != -1) { /* already registered */
        response = 2;
        goto reply;
    }

    proc_id = stuff->pid;

    if (!(exec_path = Xstrdup(path)))
        return BadAlloc;

    if (pthread_create(&thread_id, NULL, restart_client, NULL) != 0) {
        free(exec_path);
        exec_path = NULL;
        return BadAlloc;
    }

    LogMessage(X_INFO, "registered screenlocker: '%s'\n", exec_path);
reply:
    xMyextensionRegisterScreenLockerReply rep = {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .response = response,
        .exec_path_len = exec_path? strlen(exec_path) + 1: 0,
    };

    LogMessage(X_INFO, "exec len: %u\n", rep.exec_path_len);

    if (client->swapped) {
        FatalError("fixme?");
    }

    WriteToClient(client, sizeof(xMyextensionRegisterScreenLockerReply), &rep);
    /* if (exec_path) { */
    /*     LogMessage(X_INFO, "writing exec path\n"); */
    /*     WriteToClient(client, rep.exec_path_len, exec_path); */
    /* } */
    return Success;
}

static int
ProcMyextensionUnregisterScreenLocker(ClientPtr client) {
    LogMessage(X_INFO, "expected: %zu, got: %d\n",
               (sizeof(xMyextensionUnregisterScreenLockerReq) >> 2), client->req_len);
    REQUEST_SIZE_MATCH(xMyextensionUnregisterScreenLockerReq);

    if (exec_path) {
        free(exec_path);
        exec_path = NULL;
    }

    if (thread_id != -1) {
        pthread_cancel(thread_id);
        thread_id = -1;
    }

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
        case X_MyextensionRegisterScreenLocker:
            return ProcMyextensionRegisterScreenLocker(client);
        case X_MyextensionUnregisterScreenLocker:
            return ProcMyextensionUnregisterScreenLocker(client);
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
