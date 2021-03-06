/*
    Copyright (c) 2012 Martin Sustrik  All rights reserved.
    Copyright (c) 2013 GoPivotal, Inc.  All rights reserved.

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

#include "../transport.h"

#include "ep.h"
#include "sock.h"

#include "../utils/err.h"
#include "../utils/cont.h"
#include "../utils/fast.h"
#include "../utils/attr.h"

#include <string.h>

#define GRID_EP_STATE_IDLE 1
#define GRID_EP_STATE_ACTIVE 2
#define GRID_EP_STATE_STOPPING 3

#define GRID_EP_ACTION_STOPPED 1

/*  Private functions. */
static void grid_ep_handler (struct grid_fsm *self, int src, int type,
    void *srcptr);
static void grid_ep_shutdown (struct grid_fsm *self, int src, int type,
    void *srcptr);

int grid_ep_init (struct grid_ep *self, int src, struct grid_sock *sock, int eid,
    struct grid_transport *transport, int bind, const char *addr)
{
    int rc;

    grid_fsm_init (&self->fsm, grid_ep_handler, grid_ep_shutdown,
        src, self, &sock->fsm);
    self->state = GRID_EP_STATE_IDLE;

    self->epbase = NULL;
    self->sock = sock;
    self->eid = eid;
    self->last_errno = 0;
    grid_list_item_init (&self->item);
    memcpy (&self->options, &sock->ep_template, sizeof(struct grid_ep_options));

    /*  Store the textual form of the address. */
    grid_assert (strlen (addr) <= GRID_SOCKADDR_MAX);
    strcpy (self->addr, addr);

    /*  Create transport-specific part of the endpoint. */
    if (bind)
        rc = transport->bind ((void*) self, &self->epbase);
    else
        rc = transport->connect ((void*) self, &self->epbase);

    /*  Endpoint creation failed. */
    if (rc < 0) {
        grid_list_item_term (&self->item);
        grid_fsm_term (&self->fsm);
        return rc;
    }

    return 0;
}

void grid_ep_term (struct grid_ep *self)
{
    grid_assert_state (self, GRID_EP_STATE_IDLE);

    self->epbase->vfptr->destroy (self->epbase);
    grid_list_item_term (&self->item);
    grid_fsm_term (&self->fsm);
}

void grid_ep_start (struct grid_ep *self)
{
    grid_fsm_start (&self->fsm);
}

void grid_ep_stop (struct grid_ep *self)
{
    grid_fsm_stop (&self->fsm);
}

void grid_ep_stopped (struct grid_ep *self)
{
    /*  TODO: Do the following in a more sane way. */
    self->fsm.stopped.fsm = &self->fsm;
    self->fsm.stopped.src = GRID_FSM_ACTION;
    self->fsm.stopped.srcptr = NULL;
    self->fsm.stopped.type = GRID_EP_ACTION_STOPPED;
    grid_ctx_raise (self->fsm.ctx, &self->fsm.stopped);
}

struct grid_ctx *grid_ep_getctx (struct grid_ep *self)
{
    return grid_sock_getctx (self->sock);
}

const char *grid_ep_getaddr (struct grid_ep *self)
{
    return self->addr;
}

void grid_ep_getopt (struct grid_ep *self, int level, int option,
    void *optval, size_t *optvallen)
{
    int rc;

    rc = grid_sock_getopt_inner (self->sock, level, option, optval, optvallen);
    errnum_assert (rc == 0, -rc);
}

int grid_ep_ispeer (struct grid_ep *self, int socktype)
{
    return grid_sock_ispeer (self->sock, socktype);
}


static void grid_ep_shutdown (struct grid_fsm *self, int src, int type,
    GRID_UNUSED void *srcptr)
{
    struct grid_ep *ep;

    ep = grid_cont (self, struct grid_ep, fsm);

    if (grid_slow (src == GRID_FSM_ACTION && type == GRID_FSM_STOP)) {
        ep->epbase->vfptr->stop (ep->epbase);
        ep->state = GRID_EP_STATE_STOPPING;
        return;
    }
    if (grid_slow (ep->state == GRID_EP_STATE_STOPPING)) {
        if (src != GRID_FSM_ACTION || type != GRID_EP_ACTION_STOPPED)
            return;
        ep->state = GRID_EP_STATE_IDLE;
        grid_fsm_stopped (&ep->fsm, GRID_EP_STOPPED);
        return;
    }

    grid_fsm_bad_state (ep->state, src, type);
}


static void grid_ep_handler (struct grid_fsm *self, int src, int type,
    GRID_UNUSED void *srcptr)
{
    struct grid_ep *ep;

    ep = grid_cont (self, struct grid_ep, fsm);

    switch (ep->state) {

/******************************************************************************/
/*  IDLE state.                                                               */
/******************************************************************************/
    case GRID_EP_STATE_IDLE:
        switch (src) {

        case GRID_FSM_ACTION:
            switch (type) {
            case GRID_FSM_START:
                ep->state = GRID_EP_STATE_ACTIVE;
                return;
            default:
                grid_fsm_bad_action (ep->state, src, type);
            }

        default:
            grid_fsm_bad_source (ep->state, src, type);
        }

/******************************************************************************/
/*  ACTIVE state.                                                             */
/*  We don't expect any events in this state. The only thing that can be done */
/*  is closing the endpoint.                                                  */
/******************************************************************************/
    case GRID_EP_STATE_ACTIVE:
        grid_fsm_bad_source (ep->state, src, type);

/******************************************************************************/
/*  Invalid state.                                                            */
/******************************************************************************/
    default:
        grid_fsm_bad_state (ep->state, src, type);
    }
}

void grid_ep_set_error(struct grid_ep *self, int errnum)
{
    if (self->last_errno == errnum)
        /*  Error is still there, no need to report it again  */
        return;
    if (self->last_errno == 0)
        grid_sock_stat_increment (self->sock, GRID_STAT_CURRENT_EP_ERRORS, 1);
    self->last_errno = errnum;
    grid_sock_report_error (self->sock, self, errnum);
}

void grid_ep_clear_error (struct grid_ep *self)
{
    if (self->last_errno == 0)
        /*  Error is already clear, no need to report it  */
        return;
    grid_sock_stat_increment (self->sock, GRID_STAT_CURRENT_EP_ERRORS, -1);
    self->last_errno = 0;
    grid_sock_report_error (self->sock, self, 0);
}

void grid_ep_stat_increment (struct grid_ep *self, int name, int increment)
{
    grid_sock_stat_increment (self->sock, name, increment);
}
