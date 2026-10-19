-- vim._extregs: Extended Register system API
-- Internal Lua bindings for the extregister C API

local M = {}

function M.complete()
  return vim._C._extregs.get_provider_names()
end

--- Register a new extregister provider
---@param name string Provider name
---@param text_yanked_cb? fun(content:table) Callback for yank operations
---@param text_put_cb? fun():table Callback for put operations
---@return integer Provider ID on success, 0 on failure
function M.register(name, text_yanked_cb, text_put_cb)
  local ty_ref = text_yanked_cb and nlua_push_lref(global_lstate, text_yanked_cb, true) or LUA_NOREF
  local tp_ref = text_put_cb and nlua_push_lref(global_lstate, text_put_cb, true) or LUA_NOREF
  return vim._C._extregs.register(name, ty_ref, tp_ref)
end

--- Unregister an extregister provider
---@param name string Provider name to unregister
---@return boolean true on success, false if not found
function M.unregister(name)
  return vim._C._extregs.unregister(name)
end

--- Bind a character to an extregister provider
---@param alias string Character to bind (e.g., "@")
---@param provider_name string Name of provider to bind to
---@return boolean true on success, false on failure
function M.bind(alias, provider_name)
  return vim._C._extregs.bind(alias, provider_name)
end

--- Unbind a character from an extregister provider
---@param alias string Character to unbind
---@return boolean true on success, false if not bound
function M.unbind(alias)
  return vim._C._extregs.unbind(alias)
end

--- Check if a character is bound to an extregister
---@param alias string Character to check
---@return boolean true if bound, false otherwise
function M.is_alias(alias)
  return vim._C._extregs.is_alias(alias)
end

--- Get provider ID for an alias
---@param alias string Bound character
---@return integer Provider ID on success, 0 if not bound
function M.get_provider_id(alias)
  return vim._C._extregs.get_provider_id(alias)
end

--- Get provider by ID
---@param id integer Provider ID (1-based index)
---@return any? Provider on success, nil if not found
function M.get_provider_by_id(id)
  local prov = vim._C._extregs.get_provider_by_id(id)
  return prov or nil
end

--- Get provider by name
---@param name string Provider name
---@return any? Provider on success, nil if not found
function M.get_provider(name)
  local prov = vim._C._extregs.get_provider(name)
  return prov or nil
end

return M
