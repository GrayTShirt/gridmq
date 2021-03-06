/*
    Copyright (c) 2012 Martin Sustrik  All rights reserved.
    Copyright 2015 Garrett D'Amore <garrett@damore.org>

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

#include "../src/grid.h"
#include "../src/pair.h"
#include "../src/pubsub.h"
#include "../src/tcp.h"

#include "testutil.h"

/*  Tests TCP transport. */

#define SOCKET_ADDRESS "tcp://127.0.0.1:5555"

int sc;

int main ()
{
    int rc;
    int sb;
    int i;
    int opt;
    size_t sz;
    int s1, s2;
    void * dummy_buf;

    /*  Try closing bound but unconnected socket. */
    sb = test_socket (AF_SP, GRID_PAIR);
    test_bind (sb, SOCKET_ADDRESS);
    test_close (sb);

    /*  Try closing a TCP socket while it not connected. At the same time
        test specifying the local address for the connection. */
    sc = test_socket (AF_SP, GRID_PAIR);
    test_connect (sc, "tcp://127.0.0.1;127.0.0.1:5555");
    test_close (sc);

    /*  Open the socket anew. */
    sc = test_socket (AF_SP, GRID_PAIR);

    /*  Check NODELAY socket option. */
    sz = sizeof (opt);
    rc = grid_getsockopt (sc, GRID_TCP, GRID_TCP_NODELAY, &opt, &sz);
    errno_assert (rc == 0);
    grid_assert (sz == sizeof (opt));
    grid_assert (opt == 0);
    opt = 2;
    rc = grid_setsockopt (sc, GRID_TCP, GRID_TCP_NODELAY, &opt, sizeof (opt));
    grid_assert (rc < 0 && grid_errno () == EINVAL);
    opt = 1;
    rc = grid_setsockopt (sc, GRID_TCP, GRID_TCP_NODELAY, &opt, sizeof (opt));
    errno_assert (rc == 0);
    sz = sizeof (opt);
    rc = grid_getsockopt (sc, GRID_TCP, GRID_TCP_NODELAY, &opt, &sz);
    errno_assert (rc == 0);
    grid_assert (sz == sizeof (opt));
    grid_assert (opt == 1);

    /*  Try using invalid address strings. */
    rc = grid_connect (sc, "tcp://*:");
    grid_assert (rc < 0);
    errno_assert (grid_errno () == EINVAL);
    rc = grid_connect (sc, "tcp://*:1000000");
    grid_assert (rc < 0);
    errno_assert (grid_errno () == EINVAL);
    rc = grid_connect (sc, "tcp://*:some_port");
    grid_assert (rc < 0);
    rc = grid_connect (sc, "tcp://eth10000;127.0.0.1:5555");
    grid_assert (rc < 0);
    errno_assert (grid_errno () == ENODEV);
    rc = grid_connect (sc, "tcp://127.0.0.1");
    grid_assert (rc < 0);
    errno_assert (grid_errno () == EINVAL);
    rc = grid_bind (sc, "tcp://127.0.0.1:");
    grid_assert (rc < 0);
    errno_assert (grid_errno () == EINVAL);
    rc = grid_bind (sc, "tcp://127.0.0.1:1000000");
    grid_assert (rc < 0);
    errno_assert (grid_errno () == EINVAL);
    rc = grid_bind (sc, "tcp://eth10000:5555");
    grid_assert (rc < 0);
    errno_assert (grid_errno () == ENODEV);
    rc = grid_connect (sc, "tcp://:5555");
    grid_assert (rc < 0);
    errno_assert (grid_errno () == EINVAL);
    rc = grid_connect (sc, "tcp://-hostname:5555");
    grid_assert (rc < 0);
    errno_assert (grid_errno () == EINVAL);
    rc = grid_connect (sc, "tcp://abc.123.---.#:5555");
    grid_assert (rc < 0);
    errno_assert (grid_errno () == EINVAL);
    rc = grid_connect (sc, "tcp://[::1]:5555");
    grid_assert (rc < 0);
    errno_assert (grid_errno () == EINVAL);
    rc = grid_connect (sc, "tcp://abc.123.:5555");
    grid_assert (rc < 0);
    errno_assert (grid_errno () == EINVAL);
    rc = grid_connect (sc, "tcp://abc...123:5555");
    grid_assert (rc < 0);
    errno_assert (grid_errno () == EINVAL);
    rc = grid_connect (sc, "tcp://.123:5555");
    grid_assert (rc < 0);
    errno_assert (grid_errno () == EINVAL);

    /*  Connect correctly. Do so before binding the peer socket. */
    test_connect (sc, SOCKET_ADDRESS);

    /*  Leave enough time for at least on re-connect attempt. */
    grid_sleep (200);

    sb = test_socket (AF_SP, GRID_PAIR);
    test_bind (sb, SOCKET_ADDRESS);

    /*  Ping-pong test. */
    for (i = 0; i != 100; ++i) {

        test_send (sc, "ABC");
        test_recv (sb, "ABC");

        test_send (sb, "DEF");
        test_recv (sc, "DEF");
    }

    /*  Batch transfer test. */
    for (i = 0; i != 100; ++i) {
        test_send (sc, "0123456789012345678901234567890123456789");
    }
    for (i = 0; i != 100; ++i) {
        test_recv (sb, "0123456789012345678901234567890123456789");
    }

    test_close (sc);
    test_close (sb);

    /*  Test whether connection rejection is handled decently. */
    sb = test_socket (AF_SP, GRID_PAIR);
    test_bind (sb, SOCKET_ADDRESS);
    s1 = test_socket (AF_SP, GRID_PAIR);
    test_connect (s1, SOCKET_ADDRESS);
    s2 = test_socket (AF_SP, GRID_PAIR);
    test_connect (s2, SOCKET_ADDRESS);
    grid_sleep (100);
    test_close (s2);
    test_close (s1);
    test_close (sb);

    /*  Test two sockets binding to the same address. */
    sb = test_socket (AF_SP, GRID_PAIR);
    test_bind (sb, SOCKET_ADDRESS);
    s1 = test_socket (AF_SP, GRID_PAIR);
    test_bind (s1, SOCKET_ADDRESS);
    sc = test_socket (AF_SP, GRID_PAIR);
    test_connect (sc, SOCKET_ADDRESS);
    grid_sleep (100);
    test_send (sb, "ABC");
    test_recv (sc, "ABC");
    test_close (sb);
    test_send (s1, "ABC");
    test_recv (sc, "ABC");
    test_close (sc);
    test_close (s1);

    /*  Test GRID_RCVMAXSIZE limit */
    sb = test_socket (AF_SP, GRID_PAIR);
    test_bind (sb, SOCKET_ADDRESS);
    s1 = test_socket (AF_SP, GRID_PAIR);
    test_connect (s1, SOCKET_ADDRESS);
    opt = 4;
    rc = grid_setsockopt (sb, GRID_SOL_SOCKET, GRID_RCVMAXSIZE, &opt, sizeof (opt));
    grid_assert (rc == 0);
    grid_sleep (100);
    test_send (s1, "ABC");
    test_recv (sb, "ABC");
    test_send (s1, "0123456789012345678901234567890123456789");
    rc = grid_recv (sb, &dummy_buf, GRID_MSG, GRID_DONTWAIT);
    grid_assert (rc < 0);
    errno_assert (grid_errno () == EAGAIN);
    test_close (sb);
    test_close (s1);

    /*  Test that GRID_RCVMAXSIZE can be -1, but not lower */
    sb = test_socket (AF_SP, GRID_PAIR);
    opt = -1;
    rc = grid_setsockopt (sb, GRID_SOL_SOCKET, GRID_RCVMAXSIZE, &opt, sizeof (opt));
    grid_assert (rc >= 0);
    opt = -2;
    rc = grid_setsockopt (sb, GRID_SOL_SOCKET, GRID_RCVMAXSIZE, &opt, sizeof (opt));
    grid_assert (rc < 0);
    errno_assert (grid_errno () == EINVAL);
    test_close (sb);

    /*  Test closing a socket that is waiting to bind. */
    sb = test_socket (AF_SP, GRID_PAIR);
    test_bind (sb, SOCKET_ADDRESS);
    grid_sleep (100);
    s1 = test_socket (AF_SP, GRID_PAIR);
    test_bind (s1, SOCKET_ADDRESS);
    sc = test_socket (AF_SP, GRID_PAIR);
    test_connect (sc, SOCKET_ADDRESS);
    grid_sleep (100);
    test_send (sb, "ABC");
    test_recv (sc, "ABC");
    test_close (s1);
    test_send (sb, "ABC");
    test_recv (sc, "ABC");
    test_close (sb);
    test_close (sc);

    return 0;
}
