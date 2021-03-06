/*
    Copyright (c) 2012-2013 Martin Sustrik  All rights reserved.

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"),
    to deal in the Software without restriction, including without limitation
    the rights to use, copy, modify, merge, publish, distribute, sublicense,
    and/or sell copies of the Software, and to permit persons to whom
    the Software is furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included
    in all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
    THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
    IN THE SOFTWARE.
*/

#include "bipc.h"
#include "aipc.h"

#include "../../aio/fsm.h"
#include "../../aio/usock.h"

#include "../utils/backoff.h"

#include "../../utils/err.h"
#include "../../utils/cont.h"
#include "../../utils/alloc.h"
#include "../../utils/list.h"
#include "../../utils/fast.h"

#include <string.h>
#include <unistd.h>
#include <sys/un.h>
#include <fcntl.h>

#define GRID_BIPC_BACKLOG 10

#define GRID_BIPC_STATE_IDLE 1
#define GRID_BIPC_STATE_ACTIVE 2
#define GRID_BIPC_STATE_STOPPING_AIPC 3
#define GRID_BIPC_STATE_STOPPING_USOCK 4
#define GRID_BIPC_STATE_STOPPING_AIPCS 5
#define GRID_BIPC_STATE_LISTENING 6
#define GRID_BIPC_STATE_WAITING 7
#define GRID_BIPC_STATE_CLOSING 8
#define GRID_BIPC_STATE_STOPPING_BACKOFF 9

#define GRID_BIPC_SRC_USOCK 1
#define GRID_BIPC_SRC_AIPC 2
#define GRID_BIPC_SRC_RECONNECT_TIMER 3

struct grid_bipc {

    /*  The state machine. */
    struct grid_fsm fsm;
    int state;

    /*  This object is a specific type of endpoint.
        Thus it is derived from epbase. */
    struct grid_epbase epbase;

    /*  The underlying listening IPC socket. */
    struct grid_usock usock;

    /*  The connection being accepted at the moment. */
    struct grid_aipc *aipc;

    /*  List of accepted connections. */
    struct grid_list aipcs;

    /*  Used to wait before retrying to connect. */
    struct grid_backoff retry;
};

/*  grid_epbase virtual interface implementation. */
static void grid_bipc_stop (struct grid_epbase *self);
static void grid_bipc_destroy (struct grid_epbase *self);
const struct grid_epbase_vfptr grid_bipc_epbase_vfptr = {
    grid_bipc_stop,
    grid_bipc_destroy
};

/*  Private functions. */
static void grid_bipc_handler (struct grid_fsm *self, int src, int type,
    void *srcptr);
static void grid_bipc_shutdown (struct grid_fsm *self, int src, int type,
    void *srcptr);
static void grid_bipc_start_listening (struct grid_bipc *self);
static void grid_bipc_start_accepting (struct grid_bipc *self);

int grid_bipc_create (void *hint, struct grid_epbase **epbase)
{
    struct grid_bipc *self;
    int reconnect_ivl;
    int reconnect_ivl_max;
    size_t sz;

    /*  Allocate the new endpoint object. */
    self = grid_alloc (sizeof (struct grid_bipc), "bipc");
    alloc_assert (self);

    /*  Initialise the structure. */
    grid_epbase_init (&self->epbase, &grid_bipc_epbase_vfptr, hint);
    grid_fsm_init_root (&self->fsm, grid_bipc_handler, grid_bipc_shutdown,
        grid_epbase_getctx (&self->epbase));
    self->state = GRID_BIPC_STATE_IDLE;
    sz = sizeof (reconnect_ivl);
    grid_epbase_getopt (&self->epbase, GRID_SOL_SOCKET, GRID_RECONNECT_IVL,
        &reconnect_ivl, &sz);
    grid_assert (sz == sizeof (reconnect_ivl));
    sz = sizeof (reconnect_ivl_max);
    grid_epbase_getopt (&self->epbase, GRID_SOL_SOCKET, GRID_RECONNECT_IVL_MAX,
        &reconnect_ivl_max, &sz);
    grid_assert (sz == sizeof (reconnect_ivl_max));
    if (reconnect_ivl_max == 0)
        reconnect_ivl_max = reconnect_ivl;
    grid_backoff_init (&self->retry, GRID_BIPC_SRC_RECONNECT_TIMER,
        reconnect_ivl, reconnect_ivl_max, &self->fsm);
    grid_usock_init (&self->usock, GRID_BIPC_SRC_USOCK, &self->fsm);
    self->aipc = NULL;
    grid_list_init (&self->aipcs);

    /*  Start the state machine. */
    grid_fsm_start (&self->fsm);

    /*  Return the base class as an out parameter. */
    *epbase = &self->epbase;

    return 0;
}

static void grid_bipc_stop (struct grid_epbase *self)
{
    struct grid_bipc *bipc;

    bipc = grid_cont (self, struct grid_bipc, epbase);

    grid_fsm_stop (&bipc->fsm);
}

static void grid_bipc_destroy (struct grid_epbase *self)
{
    struct grid_bipc *bipc;

    bipc = grid_cont (self, struct grid_bipc, epbase);

    grid_assert_state (bipc, GRID_BIPC_STATE_IDLE);
    grid_list_term (&bipc->aipcs);
    grid_assert (bipc->aipc == NULL);
    grid_usock_term (&bipc->usock);
    grid_backoff_term (&bipc->retry);
    grid_epbase_term (&bipc->epbase);
    grid_fsm_term (&bipc->fsm);

    grid_free (bipc);
}

static void grid_bipc_shutdown (struct grid_fsm *self, int src, int type,
    void *srcptr)
{
    struct grid_bipc *bipc;
    struct grid_list_item *it;
    struct grid_aipc *aipc;

    bipc = grid_cont (self, struct grid_bipc, fsm);

    if (grid_slow (src == GRID_FSM_ACTION && type == GRID_FSM_STOP)) {
        grid_backoff_stop (&bipc->retry);
        if (bipc->aipc) {
            grid_aipc_stop (bipc->aipc);
            bipc->state = GRID_BIPC_STATE_STOPPING_AIPC;
        }
        else {
            bipc->state = GRID_BIPC_STATE_STOPPING_USOCK;
        }
    }
    if (grid_slow (bipc->state == GRID_BIPC_STATE_STOPPING_AIPC)) {
        if (!grid_aipc_isidle (bipc->aipc))
            return;
        grid_aipc_term (bipc->aipc);
        grid_free (bipc->aipc);
        bipc->aipc = NULL;
        grid_usock_stop (&bipc->usock);
        bipc->state = GRID_BIPC_STATE_STOPPING_USOCK;
    }
    if (grid_slow (bipc->state == GRID_BIPC_STATE_STOPPING_USOCK)) {
       if (!grid_usock_isidle (&bipc->usock) ||
           !grid_backoff_isidle (&bipc->retry))
            return;
        for (it = grid_list_begin (&bipc->aipcs);
              it != grid_list_end (&bipc->aipcs);
              it = grid_list_next (&bipc->aipcs, it)) {
            aipc = grid_cont (it, struct grid_aipc, item);
            grid_aipc_stop (aipc);
        }
        bipc->state = GRID_BIPC_STATE_STOPPING_AIPCS;
        goto aipcs_stopping;
    }
    if (grid_slow (bipc->state == GRID_BIPC_STATE_STOPPING_AIPCS)) {
        grid_assert (src == GRID_BIPC_SRC_AIPC && type == GRID_AIPC_STOPPED);
        aipc = (struct grid_aipc *) srcptr;
        grid_list_erase (&bipc->aipcs, &aipc->item);
        grid_aipc_term (aipc);
        grid_free (aipc);

        /*  If there are no more aipc state machines, we can stop the whole
            bipc object. */
aipcs_stopping:
        if (grid_list_empty (&bipc->aipcs)) {
            bipc->state = GRID_BIPC_STATE_IDLE;
            grid_fsm_stopped_noevent (&bipc->fsm);
            grid_epbase_stopped (&bipc->epbase);
            return;
        }

        return;
    }

    grid_fsm_bad_state(bipc->state, src, type);
}

static void grid_bipc_handler (struct grid_fsm *self, int src, int type,
    void *srcptr)
{
    struct grid_bipc *bipc;
    struct grid_aipc *aipc;

    bipc = grid_cont (self, struct grid_bipc, fsm);

    switch (bipc->state) {

/******************************************************************************/
/*  IDLE state.                                                               */
/******************************************************************************/
    case GRID_BIPC_STATE_IDLE:
        switch (src) {

        case GRID_FSM_ACTION:
            switch (type) {
            case GRID_FSM_START:
                grid_bipc_start_listening (bipc);
                return;
            default:
                grid_fsm_bad_action (bipc->state, src, type);
            }

        default:
            grid_fsm_bad_source (bipc->state, src, type);
        }

/******************************************************************************/
/*  ACTIVE state.                                                             */
/*  The execution is yielded to the aipc state machine in this state.         */
/******************************************************************************/
    case GRID_BIPC_STATE_ACTIVE:
        if (srcptr == bipc->aipc) {
            switch (type) {
            case GRID_AIPC_ACCEPTED:

                /*  Move the newly created connection to the list of existing
                    connections. */
                grid_list_insert (&bipc->aipcs, &bipc->aipc->item,
                    grid_list_end (&bipc->aipcs));
                bipc->aipc = NULL;

                /*  Start waiting for a new incoming connection. */
                grid_bipc_start_accepting (bipc);

                return;

            default:
                grid_fsm_bad_action (bipc->state, src, type);
            }
        }

        /*  For all remaining events we'll assume they are coming from one
            of remaining child aipc objects. */
        grid_assert (src == GRID_BIPC_SRC_AIPC);
        aipc = (struct grid_aipc*) srcptr;
        switch (type) {
        case GRID_AIPC_ERROR:
            grid_aipc_stop (aipc);
            return;
        case GRID_AIPC_STOPPED:
            grid_list_erase (&bipc->aipcs, &aipc->item);
            grid_aipc_term (aipc);
            grid_free (aipc);
            return;
        default:
            grid_fsm_bad_action (bipc->state, src, type);
        }

/******************************************************************************/
/*  CLOSING_USOCK state.                                                     */
/*  usock object was asked to stop but it haven't stopped yet.                */
/******************************************************************************/
    case GRID_BIPC_STATE_CLOSING:
        switch (src) {

        case GRID_BIPC_SRC_USOCK:
            switch (type) {
            case GRID_USOCK_SHUTDOWN:
                return;
            case GRID_USOCK_STOPPED:
                grid_backoff_start (&bipc->retry);
                bipc->state = GRID_BIPC_STATE_WAITING;
                return;
            default:
                grid_fsm_bad_action (bipc->state, src, type);
            }

        default:
            grid_fsm_bad_source (bipc->state, src, type);
        }

/******************************************************************************/
/*  WAITING state.                                                            */
/*  Waiting before re-bind is attempted. This way we won't overload           */
/*  the system by continuous re-bind attemps.                                 */
/******************************************************************************/
    case GRID_BIPC_STATE_WAITING:
        switch (src) {

        case GRID_BIPC_SRC_RECONNECT_TIMER:
            switch (type) {
            case GRID_BACKOFF_TIMEOUT:
                grid_backoff_stop (&bipc->retry);
                bipc->state = GRID_BIPC_STATE_STOPPING_BACKOFF;
                return;
            default:
                grid_fsm_bad_action (bipc->state, src, type);
            }

        default:
            grid_fsm_bad_source (bipc->state, src, type);
        }

/******************************************************************************/
/*  STOPPING_BACKOFF state.                                                   */
/*  backoff object was asked to stop, but it haven't stopped yet.             */
/******************************************************************************/
    case GRID_BIPC_STATE_STOPPING_BACKOFF:
        switch (src) {

        case GRID_BIPC_SRC_RECONNECT_TIMER:
            switch (type) {
            case GRID_BACKOFF_STOPPED:
                grid_bipc_start_listening (bipc);
                return;
            default:
                grid_fsm_bad_action (bipc->state, src, type);
            }

        default:
            grid_fsm_bad_source (bipc->state, src, type);
        }

/******************************************************************************/
/*  Invalid state.                                                            */
/******************************************************************************/
    default:
        grid_fsm_bad_state (bipc->state, src, type);
    }
}

/******************************************************************************/
/*  State machine actions.                                                    */
/******************************************************************************/

static void grid_bipc_start_listening (struct grid_bipc *self)
{
    int rc;
    struct sockaddr_storage ss;
    struct sockaddr_un *un;
    const char *addr;
    int fd;

    /*  First, create the AF_UNIX address. */
    addr = grid_epbase_getaddr (&self->epbase);
    memset (&ss, 0, sizeof (ss));
    un = (struct sockaddr_un*) &ss;
    grid_assert (strlen (addr) < sizeof (un->sun_path));
    ss.ss_family = AF_UNIX;
    strncpy (un->sun_path, addr, sizeof (un->sun_path));

    /*  Delete the IPC file left over by eventual previous runs of
        the application. We'll check whether the file is still in use by
        connecting to the endpoint. On Windows plaform, NamedPipe is used
        which does not have an underlying file. */
    fd = socket (AF_UNIX, SOCK_STREAM, 0);
    if (fd >= 0) {
        rc = fcntl (fd, F_SETFL, O_NONBLOCK);
        errno_assert (rc != -1 || errno == EINVAL);
        rc = connect (fd, (struct sockaddr*) &ss,
            sizeof (struct sockaddr_un));
        if (rc == -1 && errno == ECONNREFUSED) {
            rc = unlink (addr);
            errno_assert (rc == 0 || errno == ENOENT);
        }
        rc = close (fd);
        errno_assert (rc == 0);
    }

    /*  Start listening for incoming connections. */
    rc = grid_usock_start (&self->usock, AF_UNIX, SOCK_STREAM, 0);
    if (grid_slow (rc < 0)) {
        grid_backoff_start (&self->retry);
        self->state = GRID_BIPC_STATE_WAITING;
        return;
    }

    rc = grid_usock_bind (&self->usock,
        (struct sockaddr*) &ss, sizeof (struct sockaddr_un));
    if (grid_slow (rc < 0)) {
        grid_usock_stop (&self->usock);
        self->state = GRID_BIPC_STATE_CLOSING;
        return;
    }

    rc = grid_usock_listen (&self->usock, GRID_BIPC_BACKLOG);
    if (grid_slow (rc < 0)) {
        grid_usock_stop (&self->usock);
        self->state = GRID_BIPC_STATE_CLOSING;
        return;
    }
    grid_bipc_start_accepting (self);
    self->state = GRID_BIPC_STATE_ACTIVE;
}

static void grid_bipc_start_accepting (struct grid_bipc *self)
{
    grid_assert (self->aipc == NULL);

    /*  Allocate new aipc state machine. */
    self->aipc = grid_alloc (sizeof (struct grid_aipc), "aipc");
    alloc_assert (self->aipc);
    grid_aipc_init (self->aipc, GRID_BIPC_SRC_AIPC, &self->epbase, &self->fsm);

    /*  Start waiting for a new incoming connection. */
    grid_aipc_start (self->aipc, &self->usock);
}
