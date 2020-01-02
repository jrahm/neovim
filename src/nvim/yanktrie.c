#include "nvim/yanktrie.h"

#include "nvim/memory.h"

void init_yanktrie(yanktrie_T* yanktrie)
{
  memset(yanktrie, 0, sizeof(yanktrie_T));
}

yankreg_T* loose_get(yanktrie_T* trie, size_t index, int rec)
{
  int minor_index = index & 0x0F;

  if (rec == 4) {
    return &trie->u.value;
  }

  if (trie->u.children[minor_index] == NULL) {
    trie->u.children[minor_index] = (yanktrie_T*)xmalloc(sizeof(yanktrie_T));
    init_yanktrie(trie->u.children[minor_index]);
  }

  return loose_get(trie->u.children[minor_index], index >> 4, rec + 1);
}

yankreg_T* yanktrie_get(yanktrie_T* trie, size_t index)
{
  return loose_get(trie, index, 0);
}

static void yanktrie_loose_foreach(yanktrie_T* trie,
                                   void (*fn)(void*, size_t, yankreg_T*),
                                   int depth,
                                   size_t index,
                                   void* closure)
{
  size_t i;
  if (!trie)
    return;

  if (depth == 4) {
    fn(closure, index, &trie->u.value);
    return;
  } else {
    for (i = 0; i < 16; ++i) {
      yanktrie_loose_foreach(trie->u.children[i], fn, depth + 1, index << 4 | i,
                             closure);
    }
  }
}

void yanktrie_foreach(yanktrie_T* trie,
                      void (*fn)(void*, size_t, yankreg_T*),
                      void* closure)
{
  yanktrie_loose_foreach(trie, fn, 0, 0, closure);
}
