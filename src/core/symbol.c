/*
    Copyright (c) 2013 Evan Wies <evan@neomantra.net>
    Copyright (c) 2013 GoPivotal, Inc.  All rights reserved.
    Copyright (c) 2016 Bent Cardan. All rights reserved.

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

#include "../grid.h"

#include "../inproc.h"
#include "../ipc.h"
#include "../tcp.h"

#include "../pair.h"
#include "../pubsub.h"
#include "../reqrep.h"
#include "../pipeline.h"
#include "../survey.h"
#include "../bus.h"
#include "../ws.h"

#include <string.h>

static const struct grid_symbol_properties sym_value_names [] = {
    {GRID_NS_NAMESPACE, "GRID_NS_NAMESPACE", GRID_NS_NAMESPACE,
        GRID_TYPE_NONE, GRID_UNIT_NONE},
    {GRID_NS_VERSION, "GRID_NS_VERSION", GRID_NS_NAMESPACE,
        GRID_TYPE_NONE, GRID_UNIT_NONE},
    {GRID_NS_DOMAIN, "GRID_NS_DOMAIN", GRID_NS_NAMESPACE,
        GRID_TYPE_NONE, GRID_UNIT_NONE},
    {GRID_NS_TRANSPORT, "GRID_NS_TRANSPORT", GRID_NS_NAMESPACE,
        GRID_TYPE_NONE, GRID_UNIT_NONE},
    {GRID_NS_PROTOCOL, "GRID_NS_PROTOCOL", GRID_NS_NAMESPACE,
        GRID_TYPE_NONE, GRID_UNIT_NONE},
    {GRID_NS_OPTION_LEVEL, "GRID_NS_OPTION_LEVEL", GRID_NS_NAMESPACE,
        GRID_TYPE_NONE, GRID_UNIT_NONE},
    {GRID_NS_SOCKET_OPTION, "GRID_NS_SOCKET_OPTION", GRID_NS_NAMESPACE,
        GRID_TYPE_NONE, GRID_UNIT_NONE},
    {GRID_NS_TRANSPORT_OPTION, "GRID_NS_TRANSPORT_OPTION", GRID_NS_NAMESPACE,
        GRID_TYPE_NONE, GRID_UNIT_NONE},
    {GRID_NS_OPTION_TYPE, "GRID_NS_OPTION_TYPE", GRID_NS_NAMESPACE,
        GRID_TYPE_NONE, GRID_UNIT_NONE},
	 {GRID_NS_OPTION_UNIT, "GRID_NS_OPTION_UNIT", GRID_NS_NAMESPACE,
			GRID_TYPE_NONE, GRID_UNIT_NONE},
    {GRID_NS_FLAG, "GRID_NS_FLAG", GRID_NS_NAMESPACE,
        GRID_TYPE_NONE, GRID_UNIT_NONE},
    {GRID_NS_ERROR, "GRID_NS_ERROR", GRID_NS_NAMESPACE,
        GRID_TYPE_NONE, GRID_UNIT_NONE},
    {GRID_NS_LIMIT, "GRID_NS_LIMIT", GRID_NS_NAMESPACE,
        GRID_TYPE_NONE, GRID_UNIT_NONE},
    {GRID_NS_EVENT, "GRID_NS_EVENT", GRID_NS_NAMESPACE,
        GRID_TYPE_NONE, GRID_UNIT_NONE},

    {GRID_TYPE_NONE, "GRID_TYPE_NONE", GRID_NS_OPTION_TYPE,
        GRID_TYPE_NONE, GRID_UNIT_NONE},
    {GRID_TYPE_INT, "GRID_TYPE_INT", GRID_NS_OPTION_TYPE,
        GRID_TYPE_NONE, GRID_UNIT_NONE},
    {GRID_TYPE_STR, "GRID_TYPE_STR", GRID_NS_OPTION_TYPE,
        GRID_TYPE_NONE, GRID_UNIT_NONE},

    {GRID_UNIT_NONE, "GRID_UNIT_NONE", GRID_NS_OPTION_UNIT,
        GRID_TYPE_NONE, GRID_UNIT_NONE},
    {GRID_UNIT_BYTES, "GRID_UNIT_BYTES", GRID_NS_OPTION_UNIT,
        GRID_TYPE_NONE, GRID_UNIT_NONE},
    {GRID_UNIT_MILLISECONDS, "GRID_UNIT_MILLISECONDS", GRID_NS_OPTION_UNIT,
        GRID_TYPE_NONE, GRID_UNIT_NONE},
    {GRID_UNIT_PRIORITY, "GRID_UNIT_PRIORITY", GRID_NS_OPTION_UNIT,
        GRID_TYPE_NONE, GRID_UNIT_NONE},
    {GRID_UNIT_BOOLEAN, "GRID_UNIT_BOOLEAN", GRID_NS_OPTION_UNIT,
        GRID_TYPE_NONE, GRID_UNIT_NONE},

    {GRID_VERSION_CURRENT, "GRID_VERSION_CURRENT", GRID_NS_VERSION,
        GRID_TYPE_NONE, GRID_UNIT_NONE},
    {GRID_VERSION_REVISION, "GRID_VERSION_REVISION", GRID_NS_VERSION,
        GRID_TYPE_NONE, GRID_UNIT_NONE},
    {GRID_VERSION_AGE, "GRID_VERSION_AGE", GRID_NS_VERSION,
        GRID_TYPE_NONE, GRID_UNIT_NONE},

    {AF_SP, "AF_SP", GRID_NS_DOMAIN,
        GRID_TYPE_NONE, GRID_UNIT_NONE},
    {AF_SP_RAW, "AF_SP_RAW", GRID_NS_DOMAIN,
        GRID_TYPE_NONE, GRID_UNIT_NONE},

    {GRID_INPROC, "GRID_INPROC", GRID_NS_TRANSPORT,
        GRID_TYPE_NONE, GRID_UNIT_NONE},
    {GRID_IPC, "GRID_IPC", GRID_NS_TRANSPORT,
        GRID_TYPE_NONE, GRID_UNIT_NONE},
    {GRID_TCP, "GRID_TCP", GRID_NS_TRANSPORT,
        GRID_TYPE_NONE, GRID_UNIT_NONE},
    {GRID_WS, "GRID_WS", GRID_NS_TRANSPORT,
        GRID_TYPE_NONE, GRID_UNIT_NONE},

    {GRID_PAIR, "GRID_PAIR", GRID_NS_PROTOCOL,
        GRID_TYPE_NONE, GRID_UNIT_NONE},
    {GRID_PUB, "GRID_PUB", GRID_NS_PROTOCOL,
        GRID_TYPE_NONE, GRID_UNIT_NONE},
    {GRID_SUB, "GRID_SUB", GRID_NS_PROTOCOL,
        GRID_TYPE_NONE, GRID_UNIT_NONE},
    {GRID_REP, "GRID_REP", GRID_NS_PROTOCOL,
        GRID_TYPE_NONE, GRID_UNIT_NONE},
    {GRID_REQ, "GRID_REQ", GRID_NS_PROTOCOL,
        GRID_TYPE_NONE, GRID_UNIT_NONE},
    {GRID_PUSH, "GRID_PUSH", GRID_NS_PROTOCOL,
        GRID_TYPE_NONE, GRID_UNIT_NONE},
    {GRID_PULL, "GRID_PULL", GRID_NS_PROTOCOL,
        GRID_TYPE_NONE, GRID_UNIT_NONE},
    {GRID_SURVEYOR, "GRID_SURVEYOR", GRID_NS_PROTOCOL,
        GRID_TYPE_NONE, GRID_UNIT_NONE},
    {GRID_RESPONDENT, "GRID_RESPONDENT", GRID_NS_PROTOCOL,
        GRID_TYPE_NONE, GRID_UNIT_NONE},
    {GRID_BUS, "GRID_BUS", GRID_NS_PROTOCOL,
        GRID_TYPE_NONE, GRID_UNIT_NONE},

    {GRID_SOCKADDR_MAX, "GRID_SOCKADDR_MAX", GRID_NS_LIMIT,
        GRID_TYPE_NONE, GRID_UNIT_NONE},

    {GRID_SOL_SOCKET, "GRID_SOL_SOCKET", GRID_NS_OPTION_LEVEL,
        GRID_TYPE_NONE, GRID_UNIT_NONE},

    {GRID_LINGER, "GRID_LINGER", GRID_NS_SOCKET_OPTION,
        GRID_TYPE_INT, GRID_UNIT_MILLISECONDS},
    {GRID_SNDBUF, "GRID_SNDBUF", GRID_NS_SOCKET_OPTION,
        GRID_TYPE_INT, GRID_UNIT_BYTES},
    {GRID_RCVBUF, "GRID_RCVBUF", GRID_NS_SOCKET_OPTION,
        GRID_TYPE_INT, GRID_UNIT_BYTES},
    {GRID_RCVMAXSIZE, "GRID_RCVMAXSIZE", GRID_NS_SOCKET_OPTION,
        GRID_TYPE_INT, GRID_UNIT_BYTES},
    {GRID_SNDTIMEO, "GRID_SNDTIMEO", GRID_NS_SOCKET_OPTION,
        GRID_TYPE_INT, GRID_UNIT_MILLISECONDS},
    {GRID_RCVTIMEO, "GRID_RCVTIMEO", GRID_NS_SOCKET_OPTION,
        GRID_TYPE_INT, GRID_UNIT_MILLISECONDS},
    {GRID_RECONNECT_IVL, "GRID_RECONNECT_IVL", GRID_NS_SOCKET_OPTION,
        GRID_TYPE_INT, GRID_UNIT_MILLISECONDS},
    {GRID_RECONNECT_IVL_MAX, "GRID_RECONNECT_IVL_MAX", GRID_NS_SOCKET_OPTION,
        GRID_TYPE_INT, GRID_UNIT_MILLISECONDS},
    {GRID_SNDPRIO, "GRID_SNDPRIO", GRID_NS_SOCKET_OPTION,
        GRID_TYPE_INT, GRID_UNIT_PRIORITY},
    {GRID_RCVPRIO, "GRID_RCVPRIO", GRID_NS_SOCKET_OPTION,
        GRID_TYPE_INT, GRID_UNIT_PRIORITY},
    {GRID_SNDFD, "GRID_SNDFD", GRID_NS_SOCKET_OPTION,
        GRID_TYPE_INT, GRID_UNIT_NONE},
    {GRID_RCVFD, "GRID_RCVFD", GRID_NS_SOCKET_OPTION,
        GRID_TYPE_INT, GRID_UNIT_NONE},
    {GRID_DOMAIN, "GRID_DOMAIN", GRID_NS_SOCKET_OPTION,
        GRID_TYPE_INT, GRID_UNIT_NONE},
    {GRID_PROTOCOL, "GRID_PROTOCOL", GRID_NS_SOCKET_OPTION,
        GRID_TYPE_INT, GRID_UNIT_NONE},
    {GRID_IPV4ONLY, "GRID_IPV4ONLY", GRID_NS_SOCKET_OPTION,
        GRID_TYPE_INT, GRID_UNIT_BOOLEAN},
    {GRID_SOCKET_NAME, "GRID_SOCKET_NAME", GRID_NS_SOCKET_OPTION,
        GRID_TYPE_STR, GRID_UNIT_NONE},

    {GRID_SUB_SUBSCRIBE, "GRID_SUB_SUBSCRIBE", GRID_NS_TRANSPORT_OPTION,
        GRID_TYPE_STR, GRID_UNIT_NONE},
    {GRID_SUB_UNSUBSCRIBE, "GRID_SUB_UNSUBSCRIBE", GRID_NS_TRANSPORT_OPTION,
        GRID_TYPE_STR, GRID_UNIT_NONE},
    {GRID_REQ_RESEND_IVL, "GRID_REQ_RESEND_IVL", GRID_NS_TRANSPORT_OPTION,
        GRID_TYPE_INT, GRID_UNIT_MILLISECONDS},
    {GRID_SURVEYOR_DEADLINE, "GRID_SURVEYOR_DEADLINE", GRID_NS_TRANSPORT_OPTION,
        GRID_TYPE_INT, GRID_UNIT_MILLISECONDS},
    {GRID_TCP_NODELAY, "GRID_TCP_NODELAY", GRID_NS_TRANSPORT_OPTION,
        GRID_TYPE_INT, GRID_UNIT_BOOLEAN},
    {GRID_WS_MSG_TYPE, "GRID_WS_MSG_TYPE", GRID_NS_TRANSPORT_OPTION,
        GRID_TYPE_INT, GRID_UNIT_NONE},

    {GRID_DONTWAIT, "GRID_DONTWAIT", GRID_NS_FLAG,
        GRID_TYPE_NONE, GRID_UNIT_NONE},
    {GRID_WS_MSG_TYPE_TEXT, "GRID_WS_MSG_TYPE_TEXT", GRID_NS_FLAG,
        GRID_TYPE_NONE, GRID_UNIT_NONE},
    {GRID_WS_MSG_TYPE_BINARY, "GRID_WS_MSG_TYPE_BINARY", GRID_NS_FLAG,
        GRID_TYPE_NONE, GRID_UNIT_NONE},

    {GRID_POLLIN, "GRID_POLLIN", GRID_NS_EVENT, GRID_TYPE_NONE, GRID_UNIT_NONE},
    {GRID_POLLOUT, "GRID_POLLOUT", GRID_NS_EVENT, GRID_TYPE_NONE, GRID_UNIT_NONE},

    {EADDRINUSE, "EADDRINUSE", GRID_NS_ERROR, GRID_TYPE_NONE, GRID_UNIT_NONE},
    {EADDRNOTAVAIL, "EADDRNOTAVAIL", GRID_NS_ERROR, GRID_TYPE_NONE, GRID_UNIT_NONE},
    {EAFNOSUPPORT, "EAFNOSUPPORT", GRID_NS_ERROR, GRID_TYPE_NONE, GRID_UNIT_NONE},
    {EAGAIN, "EAGAIN", GRID_NS_ERROR, GRID_TYPE_NONE, GRID_UNIT_NONE},
    {EBADF, "EBADF", GRID_NS_ERROR, GRID_TYPE_NONE, GRID_UNIT_NONE},
    {ECONNREFUSED, "ECONNREFUSED", GRID_NS_ERROR, GRID_TYPE_NONE, GRID_UNIT_NONE},
    {EFAULT, "EFAULT", GRID_NS_ERROR, GRID_TYPE_NONE, GRID_UNIT_NONE},
    {EFSM, "EFSM", GRID_NS_ERROR, GRID_TYPE_NONE, GRID_UNIT_NONE},
    {EINPROGRESS, "EINPROGRESS", GRID_NS_ERROR, GRID_TYPE_NONE, GRID_UNIT_NONE},
    {EINTR, "EINTR", GRID_NS_ERROR, GRID_TYPE_NONE, GRID_UNIT_NONE},
    {EINVAL, "EINVAL", GRID_NS_ERROR, GRID_TYPE_NONE, GRID_UNIT_NONE},
    {EMFILE, "EMFILE", GRID_NS_ERROR, GRID_TYPE_NONE, GRID_UNIT_NONE},
    {ENAMETOOLONG, "ENAMETOOLONG", GRID_NS_ERROR, GRID_TYPE_NONE, GRID_UNIT_NONE},
    {ENETDOWN, "ENETDOWN", GRID_NS_ERROR, GRID_TYPE_NONE, GRID_UNIT_NONE},
    {ENOBUFS, "ENOBUFS", GRID_NS_ERROR, GRID_TYPE_NONE, GRID_UNIT_NONE},
    {ENODEV, "ENODEV", GRID_NS_ERROR, GRID_TYPE_NONE, GRID_UNIT_NONE},
    {ENOMEM, "ENOMEM", GRID_NS_ERROR, GRID_TYPE_NONE, GRID_UNIT_NONE},
    {ENOPROTOOPT, "ENOPROTOOPT", GRID_NS_ERROR, GRID_TYPE_NONE, GRID_UNIT_NONE},
    {ENOTSOCK, "ENOTSOCK", GRID_NS_ERROR, GRID_TYPE_NONE, GRID_UNIT_NONE},
    {ENOTSUP, "ENOTSUP", GRID_NS_ERROR, GRID_TYPE_NONE, GRID_UNIT_NONE},
    {EPROTO, "EPROTO", GRID_NS_ERROR, GRID_TYPE_NONE, GRID_UNIT_NONE},
    {EPROTONOSUPPORT, "EPROTONOSUPPORT", GRID_NS_ERROR, GRID_TYPE_NONE, GRID_UNIT_NONE},
    {ETERM, "ETERM", GRID_NS_ERROR, GRID_TYPE_NONE, GRID_UNIT_NONE},
    {ETIMEDOUT, "ETIMEDOUT", GRID_NS_ERROR, GRID_TYPE_NONE, GRID_UNIT_NONE},
    {EACCES, "EACCES" , GRID_NS_ERROR, GRID_TYPE_NONE, GRID_UNIT_NONE},
    {ECONNABORTED, "ECONNABORTED", GRID_NS_ERROR, GRID_TYPE_NONE, GRID_UNIT_NONE},
    {ECONNRESET, "ECONNRESET", GRID_NS_ERROR, GRID_TYPE_NONE, GRID_UNIT_NONE},
    {EHOSTUNREACH, "EHOSTUNREACH", GRID_NS_ERROR, GRID_TYPE_NONE, GRID_UNIT_NONE},
    {EMSGSIZE, "EMSGSIZE", GRID_NS_ERROR, GRID_TYPE_NONE, GRID_UNIT_NONE},
    {ENETRESET, "ENETRESET", GRID_NS_ERROR, GRID_TYPE_NONE, GRID_UNIT_NONE},
    {ENETUNREACH, "ENETUNREACH", GRID_NS_ERROR, GRID_TYPE_NONE, GRID_UNIT_NONE},
    {ENOTCONN, "ENOTCONN", GRID_NS_ERROR, GRID_TYPE_NONE, GRID_UNIT_NONE},
};

const int SYM_VALUE_NAMES_LEN = (sizeof (sym_value_names) /
    sizeof (sym_value_names [0]));


const char *grid_symbol (int i, int *value)
{
    const struct grid_symbol_properties *svn;
    if (i < 0 || i >= SYM_VALUE_NAMES_LEN) {
        errno = EINVAL;
        return NULL;
    }

    svn = &sym_value_names [i];
    if (value)
        *value = svn->value;
    return svn->name;
}

int grid_symbol_info (int i, struct grid_symbol_properties *buf, int buflen)
{
    if (i < 0 || i >= SYM_VALUE_NAMES_LEN) {
        return 0;
    }
    if (buflen > (int)sizeof (struct grid_symbol_properties)) {
        buflen = (int)sizeof (struct grid_symbol_properties);
    }
    memcpy(buf, &sym_value_names [i], buflen);
    return buflen;
}
