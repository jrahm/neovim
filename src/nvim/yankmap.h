#ifndef YANK_TRIE_H_
#define YANK_TRIE_H_

#include <stdbool.h>
#include "nvim/ops.h"
#include "nvim/map.h"

typedef struct {
  /* Register name to yank register. */
  Map(int, ptr_t) reg_to_yankreg;

  /* Yank register to register name. */
  Map(ptr_t, int) yankreg_to_reg;
} yankmap_T;

void init_yankmap(yankmap_T* yankmap);

yankreg_T* yankmap_get(yankmap_T* yankmap, int index);

yankreg_T* yankmap_put(yankmap_T* yankmap, int index);

int yankmap_find(yankmap_T* yankmap, yankreg_T* yankreg);

#endif
