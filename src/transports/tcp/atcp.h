/*
    Copyright (c) 2013 Martin Sustrik  All rights reserved.

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

#ifndef GRID_ATCP_INCLUDED
#define GRID_ATCP_INCLUDED

#include "stcp.h"

#include "../../transport.h"

#include "../../aio/fsm.h"
#include "../../aio/usock.h"

#include "../../utils/list.h"

/*  State machine handling accepted TCP sockets. */

/*  In btcp, some events are just *assumed* to come from a child atcp object.
    By using non-trivial event codes, we can do more reliable sanity checking
    in such scenarios. */
#define GRID_ATCP_ACCEPTED 34231
#define GRID_ATCP_ERROR 34232
#define GRID_ATCP_STOPPED 34233

struct grid_atcp {

    /*  The state machine. */
    struct grid_fsm fsm;
    int state;

    /*  Pointer to the associated endpoint. */
    struct grid_epbase *epbase;

    /*  Underlying socket. */
    struct grid_usock usock;

    /*  Listening socket. Valid only while accepting new connection. */
    struct grid_usock *listener;
    struct grid_fsm_owner listener_owner;

    /*  State machine that takes care of the connection in the active state. */
    struct grid_stcp stcp;

    /*  Events generated by atcp state machine. */
    struct grid_fsm_event accepted;
    struct grid_fsm_event done;

    /*  This member can be used by owner to keep individual atcps in a list. */
    struct grid_list_item item;
};

void grid_atcp_init (struct grid_atcp *self, int src,
    struct grid_epbase *epbase, struct grid_fsm *owner);
void grid_atcp_term (struct grid_atcp *self);

int grid_atcp_isidle (struct grid_atcp *self);
void grid_atcp_start (struct grid_atcp *self, struct grid_usock *listener);
void grid_atcp_stop (struct grid_atcp *self);

#endif

