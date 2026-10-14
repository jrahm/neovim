#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "nvim/lua/executor.h"  // for LuaRef
#include "nvim/register_defs.h"  // for yankreg_T, MotionType
#include "nvim/eval/typval_defs.h"  // for typval_T, dict_T
#include "nvim/hashtab_defs.h"  // for hashitem_T

#define EXTREG_MAX_PROVIDERS 20

typedef struct {
  char *name;                 ///< Provider name (e.g., "file-registry")
  char *alias;                ///< Bound character as UTF-8 string (NULL if none)
  LuaRef text_yanked_cb;      ///< Lua callback for yank operations
  LuaRef text_put_cb;         ///< Lua callback for put operations
  yankreg_T yreg;             ///< Owned yank register storage
  bool registered;            ///< Is provider currently active
  uint64_t recursion_id;      ///< Recursion guard (os_time())
} extreg_provider_t;

// Global provider storage (fixed-size array)
extern extreg_provider_t extreg_provider_list[EXTREG_MAX_PROVIDERS];
extern int extreg_provider_count;  ///< Number of active providers

// Global storage maps
extern dict_T extreg_providers;  ///< name (string) -> provider_index (number)
extern dict_T extreg_aliases;    ///< alias (1-char string) -> provider_name (string)

// Active provider for ~ register
extern extreg_provider_t *active_extreg;  ///< Currently selected extreg provider

// Provider management
extern int extreg_register(const char *name, LuaRef text_yanked_cb, LuaRef text_put_cb);
extern int extreg_unregister(const char *name);
extern int extreg_bind(const char *alias, const char *provider_name);
extern int extreg_unbind(const char *alias);
extern bool extreg_is_alias(const char *alias);
extern int extreg_get_provider_id(const char *alias);
extern extreg_provider_t *extreg_get_provider_by_id(int id);
extern extreg_provider_t *extreg_get_provider(const char *name);

// Callback execution
extern bool extreg_materialize(extreg_provider_t *prov);
extern bool extreg_commit(extreg_provider_t *prov);

// Yank register population from string
extern void extreg_yreg_from_string(extreg_provider_t *prov, const char *str, size_t len);

// Register prompt
extern int get_extreg_register(void);

// Recursion guard
extern bool extreg_is_recursing(extreg_provider_t *prov);

// Initialization
extern void extreg_init(void);
extern void extreg_clear_all(void);

// Data conversion
static inline bool yankreg_to_lua_tv(yankreg_T *reg, typval_T *tv);
static inline bool lua_tv_to_yankreg(typval_T *tv, yankreg_T *reg);

// Lua bindings
extern int nlua_extregs_register(lua_State *lstate);
extern int nlua_extregs_unregister(lua_State *lstate);
extern int nlua_extregs_bind(lua_State *lstate);
extern int nlua_extregs_unbind(lua_State *lstate);
extern int nlua_extregs_is_alias(lua_State *lstate);
extern int nlua_extregs_get_provider_id(lua_State *lstate);
extern int nlua_extregs_get_provider_by_id(lua_State *lstate);
extern int nlua_extregs_get_provider(lua_State *lstate);
