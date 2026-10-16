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

// Global storage maps
hashtab_T extreg_providers = { 0 };  ///< name (string) -> provider_index (number)
dict_T extreg_aliases = { 0 };    ///< alias (1-char string) -> provider_name (string)

// Global state for dynamic register
extreg_provider_t *active_extreg = NULL;  ///< Currently selected extreg provider

// Check if alias character is valid for binding
static bool is_valid_alias(int codepoint)
{
  if (codepoint <= 0) {
    return false;  // Invalid codepoint
  }
  
  // Reject all control characters (C0: 0x00-0x1F, DEL: 0x7F, C1: 0x80-0x9F)
  if (codepoint <= 0x1F || (codepoint >= 0x7F && codepoint <= 0x9F)) {
    return false;
  }
  
  // For ASCII printable (0x20-0x7E), validate against special characters
  if (codepoint <= 127) {
    const char *invalid = "=_#%./:;+";
    return vim_strchr(invalid, (char)codepoint) == NULL && !ascii_isdigit(codepoint)
           && (codepoint < 'A' || codepoint > 'Z') && (codepoint < 'a' || codepoint > 'z')
           && codepoint != ' ';  // Reject space explicitly
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

  hash_init(&extreg_providers);
  hash_init(&extreg_aliases.dv_hashtab);
  extreg_initialized = true;
}

// Clear all extregisters (used on exit or reset)
void extreg_clear_all(void)
{
  hash_lock(&extreg_providers);

  HASHTAB_ITER(&extreg_providers, hi, {
    extreg_free_provider(HASH_ITEM_TO_EXTREG_PROVIDER(hi));
    hash_remove(&extreg_providers, hi);
  });

  hash_clear(&extreg_providers);
  hash_unlock(&extreg_providers);
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

int get_alias_register(int alias)
{
  extreg_provider_t *prov = extreg_get_provider_for_alias(alias);
  
  if (prov) {
    active_extreg = prov;
    return '~';
  }
  
  // Not an extreg alias - clear any stale active_extreg state
  active_extreg = NULL;
  return alias;
}

int get_extreg_or_alias_register(int reg)
{
  if (reg == '~') {
    return get_extreg_register();
  } else {
    return get_alias_register(reg);
  }
}

/// Register a new extregister provider
/// @param name Unique provider name
/// @param text_yanked_cb Lua callback for yank operations (LUA_NOREF if not provided)
/// @param text_put_cb Lua callback for put operations (LUA_NOREF if not provided)
/// @return true on success, false on failure
int extreg_register(const char *name, LuaRef text_yanked_cb, LuaRef text_put_cb)
{
  assert(name != NULL);

  if (strlen(name) > 256) {
    semsg(_("E356: extregister name is too long")); // Stop nasty things from happening. with
                                                    // absurdly long register names.
    return 0;
  }

  hash_lock(&extreg_providers);

  // Check if provider already exists
  hashitem_T *hi = hash_find(&extreg_providers, name);
  if (!HASHITEM_EMPTY(hi)) {
    hash_unlock(&extreg_providers);
    semsg(_("E356: Already have an extregister named '%s'"), name);
    return 0;
  }

  extreg_provider_t* prov = xcalloc(1, sizeof(*prov) + strlen(name) + 1);
  prov->text_yanked_cb = text_yanked_cb;
  prov->text_put_cb = text_put_cb;
  prov->registered = true;
  prov->recursion_id = 0;
  strcpy(prov->name, name);
  CLEAR_POINTER(&prov->yreg);

  // Add to hash table
  hash_add(&extreg_providers, prov->name);

  hash_unlock(&extreg_providers);

  return 1;
}

/// Parse provider name from ~provider syntax
///
/// Extracts provider name from string starting with '~'.
/// Returns the provider name as an allocated string (caller must free).
/// Advances input pointer past the provider name.
///
/// @param[in]  in_arg  Input string starting with '~provider ...'
/// @param[out] out     Output: pointer to char after provider name
/// @return Allocated provider name string, or NULL on error (empty/invalid name)
char *extreg_parse_provider(char *in_arg, char **out)
  FUNC_ATTR_NONNULL_ARG(1) FUNC_ATTR_NONNULL_ARG(2) FUNC_ATTR_WARN_UNUSED_RESULT
{
  assert(*in_arg == '~');

  *out = in_arg + 1;

  // Must have something after '~'
  if (in_arg[1] == NUL) {
    return NULL;
  }

  // Allocate buffer for provider name
  size_t buf_size = 16;  // Start with small buffer
  char *buf = xmalloc(buf_size);
  size_t buf_i = 0;

  // Parse provider name: allow [A-Za-z0-9._]
  char *p = in_arg + 1;
  while (*p && (ASCII_ISALNUM(*p) || *p == '.' || *p == '_')) {
    // Expand buffer if needed
    if (buf_i >= buf_size - 1) {
      buf_size *= 2;
      buf = xrealloc(buf, buf_size);
    }
    buf[buf_i++] = *p;
    p++;
  }
  buf[buf_i] = NUL;

  // Must have at least one valid character
  if (buf_i == 0) {
    xfree(buf);
    return NULL;
  }

  *out = p;
  return buf;  // Caller must free
}

/// Unregister an extregister provider
/// @param name Provider name to unregister
/// @return true on success, false if not found
int extreg_unregister(const char *name)
{
  hash_lock(&extreg_providers);

  hashitem_T *hi = hash_find(&extreg_providers, name);
  if (HASHITEM_EMPTY(hi)) {
    hash_unlock(&extreg_providers);
    return false;
  }
  extreg_provider_t *prov = HASH_ITEM_TO_EXTREG_PROVIDER(hi);
  hash_remove(&extreg_providers, hi);

  hash_unlock(&extreg_providers);

  extreg_free_provider(prov);

  return true;
}

void extreg_free_provider(extreg_provider_t* prov)
{
  if (active_extreg == prov) {
    active_extreg = NULL;
  }
  // Free Lua callbacks
  nlua_unref_global(get_global_lstate(), prov->text_yanked_cb);
  nlua_unref_global(get_global_lstate(), prov->text_put_cb);

  free_register(&prov->yreg);
  xfree(prov);
}

/// Bind a character to an extregister provider
/// @param alias_codepoint Codepoint to bind
/// @param provider_name Name of provider to bind to
/// @return true on success, false on failure
int extreg_bind(int alias_codepoint, const char *provider_name)
{
  if (!is_valid_alias(alias_codepoint)) {
    char buf[MB_MAXBYTES + 1];
    int len = utf_char2bytes(alias_codepoint, buf);
    buf[len] = NUL;
    semsg(_("E356: '%s' cannot be used as an extregister alias"), buf);
    return false;
  }

  // Reject empty provider names
  if (!provider_name || *provider_name == NUL) {
    semsg(_("E356: provider name cannot be empty"));
    return false;
  }

  // Convert codepoint to UTF-8 string for dict storage
  char utf8_buf[MB_MAXBYTES + 1];
  int utf8_len = utf_char2bytes(alias_codepoint, utf8_buf);
  utf8_buf[utf8_len] = NUL;

  dictitem_T *di = tv_dict_item_alloc_len(utf8_buf, (size_t)utf8_len);
  di->di_tv.v_type = VAR_STRING;
  di->di_tv.vval.v_string = xstrdup(provider_name);
  tv_dict_add(&extreg_aliases, di);

  return true;
}

/// Unbind a character from an extregister provider
/// @param alias_codepoint Codepoint to unbind
/// @return true on success, false if not bound
int extreg_unbind(int alias_codepoint)
{
  dict_T *alias_dict = &extreg_aliases;

  // Convert codepoint to UTF-8 string for hash table lookup
  char utf8_buf[MB_MAXBYTES + 1];
  int utf8_len = utf_char2bytes(alias_codepoint, utf8_buf);
  utf8_buf[utf8_len] = NUL;

  // Use the UTF-8 string as the hash table key
  dictitem_T *di = tv_dict_find(alias_dict, utf8_buf, (ptrdiff_t)utf8_len);
  if (di == NULL) {
    return false;  // Not bound
  }

  // Remove from alias dict
  tv_dict_item_remove(alias_dict, di);

  return true;
}

/// Check if a character is bound to an extregister
/// @param alias_codepoint Codepoint to check
/// @return true if bound, false otherwise
bool extreg_is_alias(int alias_codepoint)
{
  // Convert codepoint to UTF-8 string for hash table lookup
  char utf8_buf[MB_MAXBYTES + 1];
  int utf8_len = utf_char2bytes(alias_codepoint, utf8_buf);
  utf8_buf[utf8_len] = NUL;

  // Use the UTF-8 string as the hash table key
  dictitem_T *di = tv_dict_find(&extreg_aliases, utf8_buf, (ptrdiff_t)utf8_len);
  return (di != NULL);
}

/// Get provider for an alias codepoint
/// @param alias_codepoint Codepoint to look up
/// @return Provider pointer if bound, NULL if not found
/// @note Returns pointer to provider in extreg_providers hash table
extreg_provider_t *extreg_get_provider_for_alias(int alias_codepoint)
{
  // Convert codepoint to UTF-8 string for hash table lookup
  char utf8_buf[MB_MAXBYTES + 1];
  int utf8_len = utf_char2bytes(alias_codepoint, utf8_buf);
  utf8_buf[utf8_len] = NUL;

  // Use the UTF-8 string as the hash table key
  dictitem_T *di = tv_dict_find(&extreg_aliases, utf8_buf, (ptrdiff_t)utf8_len);
  if (di == NULL) {
    return NULL;  // Not bound
  }

  // Look up the provider by name (no allocation needed)
  // The provider name is stored in the dict value
  return extreg_get_provider(di->di_tv.vval.v_string);
}

/// Get provider name aliased to a codepoint
/// @param alias_codepoint Codepoint to look up
/// @return Provider name string if found (dictionary-owned), NULL if not found
/// @note Do NOT free the returned string - it's owned by the dictionary
const char *extreg_get_provider_name_for_alias(int alias_codepoint)
{
  // Convert codepoint to UTF-8 string for hash table lookup
  char utf8_buf[MB_MAXBYTES + 1];
  int utf8_len = utf_char2bytes(alias_codepoint, utf8_buf);
  utf8_buf[utf8_len] = NUL;

  // Use the UTF-8 string as the hash table key
  dictitem_T *di = tv_dict_find(&extreg_aliases, utf8_buf, (ptrdiff_t)utf8_len);
  if (di == NULL) {
    return NULL;  // Not bound
  }

  // Return the provider name (dictionary-owned, do not free)
  return di->di_tv.vval.v_string;
}

/// Get provider by name
/// @param name Provider name
/// @return Provider pointer on success, NULL if not found
extreg_provider_t *extreg_get_provider(const char *name)
{
  hashitem_T *hi = hash_find(&extreg_providers, name);

  if (HASHITEM_EMPTY(hi)) {
    return NULL;
  }

  return HASH_ITEM_TO_EXTREG_PROVIDER(hi);
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
  reg->y_array = xcalloc((size_t)valid_count, sizeof(String));
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
  free_register(&prov->yreg); // Clear register before overwriting it.
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
   
  const char* type;
  switch (prov->yreg.y_type) {
  case kMTUnknown:
  case kMTCharWise:
    type = "v";
    break;
  case kMTLineWise:
    type = "V";
    break;
  case kMTBlockWise:
    type = "b";
    break;
  }
  lua_pushstring(lstate, type);
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

/// Validate that an alias argument is exactly one valid UTF-8 codepoint
/// @param alias Lua string to validate
/// @param alias_len Length of the string
/// @param codepoint Out parameter for the codepoint
/// @param err_buf Error message buffer
/// @param err_buf_len Size of error buffer
/// @return true if valid, false if invalid
static bool validate_lua_alias(const char *alias, size_t alias_len, int *codepoint,
                               char *err_buf, size_t err_buf_len)
{
  if (alias_len == 0) {
    snprintf(err_buf, err_buf_len, "alias cannot be empty");
    return false;
  }

  if (alias_len > MB_MAXBYTES) {
    snprintf(err_buf, err_buf_len, "alias too long (maximum %d bytes)", MB_MAXBYTES);
    return false;
  }

  // Get the codepoint and check that we consumed the entire string
  int bytes_consumed = utf_ptr2len(alias);

  // Check if UTF-8 is valid (bytes_consumed > 0 means valid UTF-8 sequence)
  if (bytes_consumed <= 0) {
    snprintf(err_buf, err_buf_len, "invalid UTF-8 sequence");
    return false;
  }

  // Check if there are extra characters after the first codepoint
  if ((size_t)bytes_consumed != alias_len) {
    snprintf(err_buf, err_buf_len, "alias must be exactly one character, got %zu bytes", alias_len);
    return false;
  }

  *codepoint = utf_ptr2char(alias);
  return true;
}

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
  size_t alias_len;
  const char *alias = luaL_checklstring(lstate, 1, &alias_len);
  const char *provider_name = luaL_checkstring(lstate, 2);

  int codepoint;
  char err_buf[256];
  if (!validate_lua_alias(alias, alias_len, &codepoint, err_buf, sizeof(err_buf))) {
    return luaL_error(lstate, "extregs.bind: %s", err_buf);
  }

  lua_pushboolean(lstate, extreg_bind(codepoint, provider_name));
  return 1;
}

/// Lua: vim._extregs.unbind(alias)
int nlua_extregs_unbind(lua_State *lstate)
{
  size_t alias_len;
  const char *alias = luaL_checklstring(lstate, 1, &alias_len);

  int codepoint;
  char err_buf[256];
  if (!validate_lua_alias(alias, alias_len, &codepoint, err_buf, sizeof(err_buf))) {
    return luaL_error(lstate, "extregs.unbind: %s", err_buf);
  }

  lua_pushboolean(lstate, extreg_unbind(codepoint));
  return 1;
}

/// Lua: vim._extregs.is_alias(alias)
int nlua_extregs_is_alias(lua_State *lstate)
{
  size_t alias_len;
  const char *alias = luaL_checklstring(lstate, 1, &alias_len);

  int codepoint;
  char err_buf[256];
  if (!validate_lua_alias(alias, alias_len, &codepoint, err_buf, sizeof(err_buf))) {
    return luaL_error(lstate, "extregs.is_alias: %s", err_buf);
  }

  lua_pushboolean(lstate, extreg_is_alias(codepoint));
  return 1;
}

/// Lua: vim._extregs.get_provider_name_for_alias(alias)
int nlua_extregs_get_provider_name_for_alias(lua_State *lstate)
{
  size_t alias_len;
  const char *alias = luaL_checklstring(lstate, 1, &alias_len);

  int codepoint;
  char err_buf[256];
  if (!validate_lua_alias(alias, alias_len, &codepoint, err_buf, sizeof(err_buf))) {
    return luaL_error(lstate, "extregs.get_provider_name_for_alias: %s", err_buf);
  }

  const char *provider_name = extreg_get_provider_name_for_alias(codepoint);
  
  if (provider_name) {
    lua_pushstring(lstate, provider_name);
  } else {
    lua_pushnil(lstate);
  }
  
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
