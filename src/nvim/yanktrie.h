#ifndef YANK_TRIE_H_
#define YANK_TRIE_H_

#include <stdbool.h>
#include "nvim/ops.h"

/*
 * yanktrie.h: implementation of a datastructure to hold an arbitrary number of
 * yank registers.
 */

typedef struct YANKTRIE {
  union {
    struct YANKTRIE* children[16]; /* nybble-wise trie. */
    yankreg_T value;
  } u;
} yanktrie_T;


void init_yanktrie(yanktrie_T* yanktrie);

yankreg_T* yanktrie_get(yanktrie_T* yanktrie, size_t index);

void yanktrie_foreach(
    yanktrie_T* trie, void (*fn)(void*, size_t, yankreg_T*), void* closure);

#endif
