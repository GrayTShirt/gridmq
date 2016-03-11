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

#ifndef GRID_TRIE_INCLUDED
#define GRID_TRIE_INCLUDED

#include "../../utils/int.h"

#include <stddef.h>

/*  This class implements highly memory-efficient patricia trie. */

/* Maximum length of the prefix. */
#define GRID_TRIE_PREFIX_MAX 10

/* Maximum number of children in the sparse mode. */
#define GRID_TRIE_SPARSE_MAX 8

/* 'type' is set to this value when in the dense mode. */
#define GRID_TRIE_DENSE_TYPE (GRID_TRIE_SPARSE_MAX + 1)

/*  This structure represents a node in patricia trie. It's a header to be
    followed by the array of pointers to child nodes. Each node represents
    the string composed of all the prefixes on the way from the trie root,
    including the prefix in that node. */
struct grid_trie_node
{
    /*  Number of subscriptions to the given string. */
    uint32_t refcount;

    /*  Number of elements is a sparse array, or GRID_TRIE_DENSE_TYPE in case
        the array of children is dense. */
    uint8_t type;

    /*  The node adds more characters to the string, compared to the parent
        node. If there is only a single character added, it's represented
        directly in the child array. If there's more than one character added,
        all but the last one are stored as a 'prefix'. */
    uint8_t prefix_len;
    uint8_t prefix [GRID_TRIE_PREFIX_MAX];

    /*  The array of characters pointing to individual children of the node.
        Actual pointers to child nodes are stored in the memory following
        grid_trie_node structure. */
    union {

        /*  Sparse array means that individual children are identified by
            characters stored in 'children' array. The number of characters
            in the array is specified in the 'type' field. */
        struct {
            uint8_t children [GRID_TRIE_SPARSE_MAX];
        } sparse;

        /*  Dense array means that the array of node pointers following the
            structure corresponds to a continuous list of characters starting
            by 'min' character and ending by 'max' character. The characters
            in the range that have no corresponding child node are represented
            by NULL pointers. 'nbr' is the count of child nodes. */
        struct {
            uint8_t min;
            uint8_t max;
            uint16_t nbr;
            /*  There are 4 bytes of padding here. */
        } dense;
    } u;
};
/*  The structure is followed by the array of pointers to children. */

struct grid_trie {

    /*  The root node of the trie (representing the empty subscription). */
    struct grid_trie_node *root;

};

/*  Initialise an empty trie. */
void grid_trie_init (struct grid_trie *self);

/*  Release all the resources associated with the trie. */
void grid_trie_term (struct grid_trie *self);

/*  Add the string to the trie. If the string is not yet there, 1 is returned.
    If it already exists in the trie, its reference count is incremented and
    0 is returned. */
int grid_trie_subscribe (struct grid_trie *self, const uint8_t *data, size_t size);

/*  Remove the string from the trie. If the string was actually removed,
    1 is returned. If reference count was decremented without falling to zero,
    0 is returned. */
int grid_trie_unsubscribe (struct grid_trie *self, const uint8_t *data,
    size_t size);

/*  Checks the supplied string. If it matches it returns 1, if it does not
    it returns 0. */
int grid_trie_match (struct grid_trie *self, const uint8_t *data, size_t size);

/*  Debugging interface. */
void grid_trie_dump (struct grid_trie *self);

#endif

