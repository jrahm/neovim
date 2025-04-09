local M = {}

-- Table mapping register names (strings) to handler objects
-- Each handler must implement `yank(regname, contents)` and/or `put(regname)`
M.handlers = {}

-- Default handler: stores register content in memory
local default_handler = {
  registers = {}
}

--- Called when a register is yanked into (i.e., written)
---@param regname string
---@param contents table
function default_handler.yank(regname, contents)
  default_handler.registers[regname] = contents
end

--- Called when a register is put from (i.e., read)
---@param regname string
---@return string|table
function default_handler.put(regname)
  return default_handler.registers[regname] or ""
end

--- Register a handler function for one or more register names.
--- @param register_names string|string[] A register or list of registers
--- @param handler table A table with `yank()` and/or `put()` methods
function M.register_handler(register_names, handler)
  if type(register_names) == "string" then
    M.handlers[register_names] = handler
  elseif type(register_names) == "table" then
    for _, reg in ipairs(register_names) do
      M.handlers[reg] = handler
    end
  else
    error("register_names must be a string or table of strings")
  end
end

--- This is the function Neovim will call via 'userregfunc'.
--- It dispatches based on the action and register name.
---@param action string "yank" or "put"
---@param regname string
---@param contents any Only passed when action is "yank"
---@return any Only returned when action is "put"
function M.fn(action, regname, contents)
  local handler = M.handlers[regname] or default_handler
  if action == "yank" and handler.yank then
    handler.yank(regname, contents)
  elseif action == "put" and handler.put then
    return handler.put(regname)
  else
    vim.notify(
      ("[userregs] No valid handler for action %q on register %q"):format(action, regname),
      vim.log.levels.WARN
    )
  end
end

-- Expose the function to Neovim via 'set userregfunc=v:lua.def_userreg_func'
_G.def_userreg_func = M.fn

return M
