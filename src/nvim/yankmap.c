#include "nvim/yankmap.h"

#include "nvim/memory.h"

void init_yankmap(yankmap_T* map)
{
  memset(map, 0, sizeof(yankmap_T));

  map_init(int, ptr_t, &map->reg_to_yankreg);
  map_init(ptr_t, int, &map->yankreg_to_reg);
}

yankreg_T* yankmap_get(yankmap_T* yankmap, int reg)
{
  yankreg_T** ret =
    (yankreg_T**) map_ref(int, ptr_t)(&yankmap->reg_to_yankreg, reg, true);

  if (ret) {
    if (! (*ret)) {
      *ret = xcalloc(sizeof(yankreg_T), 1);
    }

    /* Add the back-reference */
    int* ref = map_ref(ptr_t, int)(&yankmap->yankreg_to_reg, *ret, true);
    *ref = reg;

    return *ret;
  }

  return NULL;
}

int yankmap_find(yankmap_T* yankmap, yankreg_T* yankreg)
{
  int* ref = map_ref(ptr_t, int)(&yankmap->yankreg_to_reg, yankreg, false);

  if (ref) {
    return *ref;
  }

  return -1;
}
