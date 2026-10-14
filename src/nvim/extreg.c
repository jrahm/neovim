// Extended Register (Extregister) System
// Allows Lua callbacks to intercept yank and put operations for custom storage backends

#include "nvim/extreg.h"

#include "nvim/ascii_defs.h"
#include "nvim/errors.h"
#include "nvim/eval/typval.h"
#include "nvim/eval/typval_defs.h"
#include "nvim/ex_getln.h"
#include "nvim/globals.h"
#include "nvim/hashtab.h"
#include "nvim/lua/converter.h"
#include "nvim/lua/executor.h"
#include "nvim/mbyte.h"
#include "nvim/memline.h"
#include "nvim/memory.h"
#include "nvim/ops.h"
#include "nvim/os/time.h"
#include "nvim/register.h"
#include "nvim/strings.h"
#include "nvim/types_defs.h"

#include "extreg.c.generated.h"

// Global provider storage (fixed-size array)
extreg_provider_t extreg_provider_list[EXTREG_MAX_PROVIDERS] = { 0 };
int extreg_provider_count = 0;

// Global storage maps
dict_T extreg_providers = { 0 };  ///< name (string) -> provider_index (number)
dict_T extreg_aliases = { 0 };    ///< alias (1-char string) -> provider_name (string)

// Global state for dynamic register
extreg_provider_t *active_extreg = NULL;  ///< Currently selected extreg provider

// Check if alias character is valid for binding
static bool is_valid_alias(const char *alias)
{
  if (alias == NULL || *alias == NUL) {
    return false;
  }
  int codepoint = utf_ptr2char(alias);
  if (codepoint < 0) {
    return false;  // Invalid UTF-8
  }
  // For ASCII (0-127), validate against special characters
  if (codepoint <= 127) {
    const char *invalid = "=_#%./:;+";
    return vim_strchr(invalid, (char)codepoint) == NULL && !ascii_isdigit(codepoint)
           && (codepoint < 'A' || codepoint > 'Z') && (codepoint < 'a' || codepoint > 'z')
           && !ascii_iswhite(codepoint);
  }
  // For non-ASCII Unicode codepoints, allow anything in valid Unicode range
  // except private use areas and surrogates
  if (codepoint > 0x10FFFF) {
    return false;  // Invalid Unicode
  }
  // Reject private use areas
  if ((codepoint >= 0xE000 && codepoint <= 0xF8FF) ||      // PUA
      (codepoint >= 0xF0000 && codepoint <= 0xFFFFD) ||    // Supplementary PUA
      (codepoint >= 0x100000 && codepoint <= 0x10FFFD)) {  // Supplementary PUA
    return false;
  }
  // Reject surrogates
  if (codepoint >= 0xD800 && codepoint <= 0xDFFF) {
    return false;
  }
  return true;
}

// Global state flag
static bool extreg_initialized = false;

// Initialize extregister system
void extreg_init(void)
{
  if (extreg_initialized) {
    return;  // Already initialized
  }

  hash_init(&extreg_providers.dv_hashtab);
  hash_init(&extreg_aliases.dv_hashtab);
  extreg_initialized = true;
}

// Clear all extregisters (used on exit or reset)
void extreg_clear_all(void)
{
  // Free all providers
  for (int i = 0; i < extreg_provider_count; i++) {
    extreg_provider_t *prov = &extreg_provider_list[i];
    if (prov->registered) {
      nlua_unref_global(get_global_lstate(), prov->text_yanked_cb);
      nlua_unref_global(get_global_lstate(), prov->text_put_cb);
      free_register(&prov->yreg);
      xfree(prov->name);
      xfree(prov->alias);
    }
  }

  // Clear dicts if initialized
  if (extreg_providers.dv_hashtab.ht_array != NULL) {
    tv_dict_free(&extreg_providers);
  }
  if (extreg_aliases.dv_hashtab.ht_array != NULL) {
    tv_dict_free(&extreg_aliases);
  }

  // Reset provider list
  memset(extreg_provider_list, 0, sizeof(extreg_provider_list));
  extreg_provider_count = 0;
  active_extreg = NULL;        // Clear active provider
  extreg_initialized = false;  // Reset initialization flag
}

/// Prompt user to select an extregister provider
/// @return '~' if successful, NUL if cancelled or error
int get_extreg_register(void)
{
  char *provider_name = getcmdline('~', 0, 0, true);
  if (provider_name == NULL) {
    active_extreg = NULL;
    return NUL;  // User cancelled with <Esc>
  }
  if (*provider_name == NUL) {
    xfree(provider_name);
    return '~';  // Use previously selected provider
  }
  active_extreg = extreg_get_provider(provider_name);
  xfree(provider_name);
  if (active_extreg == NULL) {
    semsg(_("E356: Unknown extregister name"));
    return NUL;
  }
  return '~';  // Use extreg register
}

/// Register a new extregister provider
/// @param name Unique provider name
/// @param text_yanked_cb Lua callback for yank operations (LUA_NOREF if not provided)
/// @param text_put_cb Lua callback for put operations (LUA_NOREF if not provided)
/// @return Provider ID on success, 0 on failure
int extreg_register(const char *name, LuaRef text_yanked_cb, LuaRef text_put_cb)
{
  assert(name != NULL);
  dict_T *prov_dict = &extreg_providers;

  // Check if provider already exists
  dictitem_T *existing = tv_dict_find(prov_dict, name, strlen(name));
  if (existing != NULL) {
    semsg(_("E356: Already have an extregister named '%s'"), name);
    return 0;
  }

  // Check if we have space
  if (extreg_provider_count >= EXTREG_MAX_PROVIDERS) {
    semsg(_("E356: Too many extregister providers (max %d)"), EXTREG_MAX_PROVIDERS);
    return 0;
  }

  // Allocate provider slot
  int idx = extreg_provider_count++;
  extreg_provider_t *prov = &extreg_provider_list[idx];
  memset(prov, 0, sizeof(*prov));

  prov->name = xstrdup(name);
  prov->alias = NUL;
  prov->text_yanked_cb = text_yanked_cb;
  prov->text_put_cb = text_put_cb;
  prov->registered = true;
  prov->recursion_id = 0;
  CLEAR_POINTER(&prov->yreg);

  // Add to dict: name -> index
  dictitem_T *di = tv_dict_item_alloc(name);
  di->di_tv.v_type = VAR_NUMBER;
  di->di_tv.vval.v_number = idx + 1;  // Use 1-based index (0 means invalid)
  tv_dict_add(prov_dict, di);

  // Return provider ID (1-based index)
  return idx + 1;
}

/// Unregister an extregister provider
/// @param name Provider name to unregister
/// @return true on success, false if not found
int extreg_unregister(const char *name)
{
  dict_T *prov_dict = &extreg_providers;

  dictitem_T *di = tv_dict_find(prov_dict, name, strlen(name));
  if (di == NULL) {
    semsg(_("E356: Unknown extregister name '%s'"), name);
    return false;
  }

  int idx = di->di_tv.vval.v_number - 1;  // Convert back to 0-based index
  if (idx < 0 || idx >= EXTREG_MAX_PROVIDERS) {
    semsg(_("E356: Invalid extregister index '%d'"), idx);
    return false;
  }

  extreg_provider_t *prov = &extreg_provider_list[idx];

  // Unbind alias if set - use UTF-8 string directly
  if (prov->alias != NULL) {
    extreg_unbind(prov->alias);
  }

  // Free Lua callbacks
  nlua_unref_global(get_global_lstate(), prov->text_yanked_cb);
  nlua_unref_global(get_global_lstate(), prov->text_put_cb);

  // Free yank register contents
  free_register(&prov->yreg);

  // Free alias string
  xfree(prov->alias);

  // Remove from dict
  tv_dict_item_remove(prov_dict, di);
  xfree(prov->name);

  // Clear provider
  memset(prov, 0, sizeof(*prov));

  return true;
}

/// Bind a character to an extregister provider
/// @param alias String containing single UTF-8 character to bind
/// @param provider_name Name of provider to bind to
/// @return true on success, false on failure
int extreg_bind(const char *alias, const char *provider_name)
{
  if (!is_valid_alias(alias)) {
    semsg(_("E356: '%s' cannot be used as an extregister alias"), alias);
    return false;
  }

  dict_T *prov_dict = &extreg_providers;
  dict_T *alias_dict = &extreg_aliases;

  // Check if provider exists
  dictitem_T *prov_di = tv_dict_find(prov_dict, provider_name, strlen(provider_name));
  if (prov_di == NULL) {
    semsg(_("E356: Unknown extregister name '%s'"), provider_name);
    return false;
  }

  int idx = prov_di->di_tv.vval.v_number - 1;  // Convert to 0-based index
  if (idx < 0 || idx >= EXTREG_MAX_PROVIDERS) {
    semsg(_("E356: Invalid extregister index '%d'"), idx);
    return false;
  }

  extreg_provider_t *prov = &extreg_provider_list[idx];

  // Unbind old provider if alias was already bound
  // Use the original UTF-8 string as the hash table key
  dictitem_T *old_di = tv_dict_find(alias_dict, alias, strlen(alias));
  if (old_di != NULL) {
    const char *old_name = old_di->di_tv.vval.v_string;
    dictitem_T *old_prov_di = tv_dict_find(prov_dict, old_name, strlen(old_name));
    if (old_prov_di != NULL) {
      int old_idx = old_prov_di->di_tv.vval.v_number - 1;
      if (old_idx >= 0 && old_idx < EXTREG_MAX_PROVIDERS) {
        extreg_provider_t *old_prov = &extreg_provider_list[old_idx];
        xfree(old_prov->alias);
        old_prov->alias = NULL;
      }
    }
    tv_dict_item_remove(alias_dict, old_di);
  }

  // Free old alias if any
  xfree(prov->alias);

  // Bind new provider - store UTF-8 string directly
  prov->alias = xstrdup(alias);

  // Create hash table entry with UTF-8 string as key
  dictitem_T *di = tv_dict_item_alloc(alias);
  di->di_tv.v_type = VAR_STRING;
  di->di_tv.vval.v_string = xstrdup(provider_name);
  tv_dict_add(alias_dict, di);

  return true;
}

/// Unbind a character from an extregister provider
/// @param alias UTF-8 string containing character to unbind
/// @return true on success, false if not bound
int extreg_unbind(const char *alias)
{
  dict_T *alias_dict = &extreg_aliases;
  dict_T *prov_dict = &extreg_providers;

  // Use the UTF-8 string directly as the hash table key
  dictitem_T *di = tv_dict_find(alias_dict, alias, strlen(alias));
  if (di == NULL) {
    return false;  // Not bound
  }

  // Clear alias in provider
  const char *prov_name = di->di_tv.vval.v_string;
  dictitem_T *prov_di = tv_dict_find(prov_dict, prov_name, strlen(prov_name));
  if (prov_di != NULL) {
    int idx = prov_di->di_tv.vval.v_number - 1;
    if (idx >= 0 && idx < EXTREG_MAX_PROVIDERS) {
      extreg_provider_t *prov = &extreg_provider_list[idx];
      xfree(prov->alias);
      prov->alias = NULL;
    }
  }

  // Remove from alias dict
  tv_dict_item_remove(alias_dict, di);

  return true;
}

/// Check if a character is bound to an extregister
/// @param alias UTF-8 string containing character to check
/// @return true if bound, false otherwise
bool extreg_is_alias(const char *alias)
{
  // Use the UTF-8 string directly as the hash table key
  dictitem_T *di = tv_dict_find(&extreg_aliases, alias, strlen(alias));
  return (di != NULL);
}

/// Get provider ID for an alias
/// @param alias UTF-8 string containing bound character
/// @return Provider ID on success, 0 if not bound
int extreg_get_provider_id(const char *alias)
{
  // Use the UTF-8 string directly as the hash table key
  dictitem_T *di = tv_dict_find(&extreg_aliases, alias, strlen(alias));
  if (di == NULL) {
    return 0;
  }

  const char *prov_name = di->di_tv.vval.v_string;
  dict_T *prov_dict = &extreg_providers;
  dictitem_T *prov_di = tv_dict_find(prov_dict, prov_name, strlen(prov_name));
  if (prov_di == NULL) {
    return 0;
  }

  return prov_di->di_tv.vval.v_number;  // Return 1-based index
}

/// Get provider by ID
/// @param id Provider ID (1-based index)
/// @return Provider pointer on success, NULL if not found
extreg_provider_t *extreg_get_provider_by_id(int id)
{
  if (id <= 0 || id > EXTREG_MAX_PROVIDERS) {
    return NULL;
  }

  extreg_provider_t *prov = &extreg_provider_list[id - 1];
  if (!prov->registered) {
    return NULL;
  }
  return prov;
}

/// Get provider by name
/// @param name Provider name
/// @return Provider pointer on success, NULL if not found
extreg_provider_t *extreg_get_provider(const char *name)
{
  dict_T *prov_dict = &extreg_providers;
  dictitem_T *di = tv_dict_find(prov_dict, name, strlen(name));
  if (di == NULL || di->di_tv.v_type != VAR_NUMBER) {
    return NULL;
  }

  int idx = di->di_tv.vval.v_number - 1;
  if (idx < 0 || idx >= EXTREG_MAX_PROVIDERS) {
    return NULL;
  }

  extreg_provider_t *prov = &extreg_provider_list[idx];
  if (!prov->registered) {
    return NULL;
  }
  return prov;
}

/// Check if provider is currently recursing
/// @param prov Provider to check
/// @return true if recursing, false otherwise
bool extreg_is_recursing(extreg_provider_t *prov)
{
  if (prov == NULL) {
    return false;
  }
  return (prov->recursion_id != 0 && prov->recursion_id != os_time());
}

/// Convert Lua typval to yankreg_T
/// @param tv Source Lua typval (must be dict or string)
/// @param[out] reg Output yankreg_T
/// @return true on success, false on error
static bool lua_tv_to_yankreg(typval_T *tv, yankreg_T *reg)
{
  CLEAR_POINTER(reg);

  // Handle string shorthand
  if (tv->v_type == VAR_STRING) {
    reg->y_size = 1;
    reg->y_type = kMTCharWise;
    reg->y_array = xcalloc(1, sizeof(String));
    reg->y_array[0] = cbuf_to_string(tv->vval.v_string, strlen(tv->vval.v_string));
    reg->timestamp = os_time();
    return true;
  }

  // Must be a dict
  if (tv->v_type != VAR_DICT) {
    semsg("extreg: text_put must return a dict or string, got %s",
          tv->v_type == VAR_LIST ? "list" : "invalid type");
    return false;
  }

  dict_T *d = tv->vval.v_dict;

  // Extract lines array
  dictitem_T *di_lines = tv_dict_find(d, "lines", 5);
  if (!di_lines || di_lines->di_tv.v_type != VAR_LIST) {
    semsg("extreg: text_put must return a dict with 'lines' as a list");
    return false;
  }

  // Validate and count lines
  list_T *lines = di_lines->di_tv.vval.v_list;
  ptrdiff_t valid_count = 0;
  listitem_T *item = tv_list_first(lines);
  while (item) {
    if (item->li_tv.v_type == VAR_STRING) {
      valid_count++;
    }
    item = item->li_next;
  }

  if (valid_count == 0) {
    // Empty register is valid
    reg->y_size = 0;
    reg->y_type = kMTCharWise;
    reg->y_array = NULL;
    reg->timestamp = os_time();
    return true;
  }

  // Extract type
  MotionType motion_type = kMTCharWise;
  dictitem_T *di_type = tv_dict_find(d, "type", 4);
  if (di_type && di_type->di_tv.v_type == VAR_STRING) {
    const char *type_str = di_type->di_tv.vval.v_string;
    if (strequal(type_str, "v")) {
      motion_type = kMTCharWise;
    } else if (strequal(type_str, "V")) {
      motion_type = kMTLineWise;
    } else if (strequal(type_str, "b")) {
      motion_type = kMTBlockWise;
    } else {
      semsg("extreg: invalid type '%s', must be 'v', 'V', or 'b'", type_str);
      return false;
    }
  }

  // Extract width (for blockwise)
  colnr_T width = 0;
  dictitem_T *di_width = tv_dict_find(d, "width", 5);
  if (di_width && motion_type == kMTBlockWise) {
    if (di_width->di_tv.v_type == VAR_NUMBER) {
      width = (colnr_T)di_width->di_tv.vval.v_number;
      if (width <= 0) {
        semsg("extreg: width must be > 0");
        return false;
      }
    } else {
      semsg("extreg: width must be a number");
      return false;
    }
  }

  // Allocate yank register
  reg->y_size = (size_t)valid_count;
  reg->y_type = motion_type;
  reg->y_width = width;
  reg->y_array = xcalloc(valid_count, sizeof(String));
  reg->timestamp = os_time();

  // Copy lines
  item = tv_list_first(lines);
  size_t idx = 0;
  while (item && idx < (size_t)valid_count) {
    if (item->li_tv.v_type == VAR_STRING) {
      reg->y_array[idx++]
        = cbuf_to_string(item->li_tv.vval.v_string, strlen(item->li_tv.vval.v_string));
    }
    item = item->li_next;
  }

  return true;
}

/// Convert yankreg_T to Lua typval
/// @param reg Source yankreg_T
/// @param[out] tv Output typval (caller must free with tv_clear)
/// @return true on success, false on error
static bool yankreg_to_lua_tv(yankreg_T *reg, typval_T *tv)
{
  tv_init(tv);
  tv->v_type = VAR_DICT;
  tv->vval.v_dict = tv_dict_alloc();

  if (reg->y_size == 0 || reg->y_array == NULL) {
    return true;
  }

  // Create lines list
  list_T *lines = tv_list_alloc((ptrdiff_t)reg->y_size);
  for (size_t i = 0; i < reg->y_size; i++) {
    typval_T item_tv;
    tv_init(&item_tv);
    item_tv.v_type = VAR_STRING;
    item_tv.vval.v_string = xmallocz(reg->y_array[i].size + 1);
    memcpy(item_tv.vval.v_string, reg->y_array[i].data, reg->y_array[i].size);
    tv_list_append_tv(lines, &item_tv);
    // tv_list_append_tv takes ownership, don't free item_tv
  }
  tv_dict_add_list(tv->vval.v_dict, S_LEN("lines"), lines);

  // Set type
  char type_char = 'v';  // Default to charwise
  if (reg->y_type == kMTLineWise) {
    type_char = 'V';
  } else if (reg->y_type == kMTBlockWise) {
    type_char = 'b';
  }

  typval_T type_tv;
  tv_init(&type_tv);
  type_tv.v_type = VAR_STRING;
  type_tv.vval.v_string = xmallocz(2);
  type_tv.vval.v_string[0] = type_char;
  type_tv.vval.v_string[1] = NUL;
  tv_dict_add_tv(tv->vval.v_dict, S_LEN("type"), &type_tv);
  tv_clear(&type_tv);

  // Set width (for blockwise)
  if (reg->y_type == kMTBlockWise) {
    tv_dict_add_nr(tv->vval.v_dict, S_LEN("width"), (varnumber_T)(reg->y_width + 1));
  }

  return true;
}

/// Materialize extregister content by calling Lua text_put callback
/// @param prov Provider to materialize
/// @param[out] out_reg Output yankreg_T to populate
/// @return true on success, false on error
bool extreg_materialize(extreg_provider_t *prov)
{
  assert(prov != NULL);

  // Check callback exists
  if (prov->text_put_cb == LUA_NOREF) {
    semsg("extreg '%s': no text_put callback", prov->name);
    return false;
  }

  // Recursion guard
  if (extreg_is_recursing(prov)) {
    semsg("extreg '%s': recursive text_put call", prov->name);
    return false;
  }
  prov->recursion_id = os_time();

  // Call Lua callback and get return value
  lua_State *lstate = get_global_lstate();
  lua_rawgeti(lstate, LUA_REGISTRYINDEX, prov->text_put_cb);
  lua_pushvalue(lstate, -1);  // Duplicate function

  // Use lua_pcall instead of nlua_call_ref to handle table returns with metatables
  if (lua_pcall(lstate, 0, 1, 0) != 0) {
    semsg("extreg '%s': error in text_put callback: %s", prov->name, lua_tostring(lstate, -1));
    prov->recursion_id = 0;
    return false;
  }

  // Pop return value
  typval_T tv;
  nlua_pop_typval(lstate, &tv);

  // Convert Lua dict to yankreg_T and store in provider's yreg
  if (!lua_tv_to_yankreg(&tv, &prov->yreg)) {
    tv_clear(&tv);
    prov->recursion_id = 0;
    return false;
  }

  tv_clear(&tv);
  prov->recursion_id = 0;
  return true;
}

/// Commit yankreg_T content by calling Lua text_yanked callback
/// @param prov Provider to commit to
/// @param in_reg Input yankreg_T with content to commit
/// @return true on success or no-op, false on error
bool extreg_commit(extreg_provider_t *prov)
{
  assert(prov != NULL);

  // Check if callback exists
  if (prov->text_yanked_cb == LUA_NOREF) {
    return true;  // No-op if no callback
  }

  // Recursion guard
  if (extreg_is_recursing(prov)) {
    semsg("extreg '%s': recursive text_yanked call", prov->name);
    return false;
  }
  prov->recursion_id = os_time();

  // Validate the yankreg data
  if (prov->yreg.y_size <= 0) {
    semsg("extreg '%s': no text to yank (size=%ld)", prov->name, (long)prov->yreg.y_size);
    prov->recursion_id = 0;
    return true;  // No-op is still success
  }
  if (prov->yreg.y_array == NULL) {
    semsg("extreg '%s': no text data (y_array is NULL)", prov->name);
    prov->recursion_id = 0;
    return true;  // No-op is still success
  }

  // Call Lua callback with yankreg content as a Lua table
  lua_State *lstate = get_global_lstate();
  lua_rawgeti(lstate, LUA_REGISTRYINDEX, prov->text_yanked_cb);
  lua_pushvalue(lstate, -1);  // Duplicate function

  // Push yankreg as a Lua table
  lua_createtable(lstate, (int)prov->yreg.y_size, 2);  // array + 2 fields
  lua_pushinteger(lstate, (lua_Integer)prov->yreg.y_type);
  lua_setfield(lstate, -2, "type");
  lua_pushinteger(lstate, (lua_Integer)prov->yreg.y_size);
  lua_setfield(lstate, -2, "size");

  // Add lines to array part
  lua_createtable(lstate, (int)prov->yreg.y_size, 0);
  for (size_t i = 0; i < prov->yreg.y_size; i++) {
    lua_pushlstring(lstate, prov->yreg.y_array[i].data, prov->yreg.y_array[i].size);
    lua_rawseti(lstate, -2, (int)(i + 1));
  }
  lua_setfield(lstate, -2, "lines");

  // Call with 1 argument (the yankreg table), expect 1 return (boolean)
  if (lua_pcall(lstate, 1, 1, 0) != 0) {
    semsg("extreg '%s': error in text_yanked callback: %s", prov->name, lua_tostring(lstate, -1));
    prov->recursion_id = 0;
    return false;
  }

  // Pop return value (boolean)
  if (!lua_isboolean(lstate, -1)) {
    semsg("extreg '%s': text_yanked callback must return boolean", prov->name);
    prov->recursion_id = 0;
    return false;
  }
  bool callback_result = lua_toboolean(lstate, -1);
  lua_pop(lstate, 1);

  prov->recursion_id = 0;
  return callback_result;
}

// Lua binding functions
#include "nvim/lua/executor.h"

/// Lua: vim._extregs.register(name, text_yanked_cb?, text_put_cb?)
int nlua_extregs_register(lua_State *lstate)
{
  const char *name = luaL_checkstring(lstate, 1);
  luaL_checktype(lstate, 2, LUA_TFUNCTION);
  luaL_checkany(lstate, 3);  // Optional

  LuaRef ty_ref = nlua_ref_global(lstate, -2);
  LuaRef tp_ref
    = lua_gettop(lstate) >= 3 && !lua_isnil(lstate, 3) ? nlua_ref_global(lstate, -1) : LUA_NOREF;

  lua_pushinteger(lstate, extreg_register(name, ty_ref, tp_ref));
  return 1;
}

/// Lua: vim._extregs.unregister(name)
int nlua_extregs_unregister(lua_State *lstate)
{
  const char *name = luaL_checkstring(lstate, 1);
  lua_pushboolean(lstate, extreg_unregister(name));
  return 1;
}

/// Lua: vim._extregs.bind(alias, provider_name)
int nlua_extregs_bind(lua_State *lstate)
{
  const char *alias = luaL_checkstring(lstate, 1);
  const char *provider_name = luaL_checkstring(lstate, 2);
  lua_pushboolean(lstate, extreg_bind(alias, provider_name));
  return 1;
}

/// Lua: vim._extregs.unbind(alias)
int nlua_extregs_unbind(lua_State *lstate)
{
  const char *alias = luaL_checkstring(lstate, 1);
  lua_pushboolean(lstate, extreg_unbind(alias));
  return 1;
}

/// Lua: vim._extregs.is_alias(alias)
int nlua_extregs_is_alias(lua_State *lstate)
{
  const char *alias = luaL_checkstring(lstate, 1);
  lua_pushboolean(lstate, extreg_is_alias(alias));
  return 1;
}

/// Lua: vim._extregs.get_provider_id(alias)
int nlua_extregs_get_provider_id(lua_State *lstate)
{
  const char *alias = luaL_checkstring(lstate, 1);
  lua_pushinteger(lstate, extreg_get_provider_id(alias));
  return 1;
}

/// Lua: vim._extregs.get_provider_by_id(id)
int nlua_extregs_get_provider_by_id(lua_State *lstate)
{
  int id = luaL_checkinteger(lstate, 1);
  extreg_provider_t *prov = extreg_get_provider_by_id(id);
  lua_pushlightuserdata(lstate, (void *)prov);
  return 1;
}

/// Lua: vim._extregs.get_provider(name)
int nlua_extregs_get_provider(lua_State *lstate)
{
  const char *name = luaL_checkstring(lstate, 1);
  extreg_provider_t *prov = extreg_get_provider(name);
  lua_pushlightuserdata(lstate, (void *)prov);
  return 1;
}
