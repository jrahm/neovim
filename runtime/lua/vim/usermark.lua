-- Defualt implementation of the usermarkfunc. This default implementation is
-- extensible and allows other plugins to register handlers for different
-- registers.
--
-- The default handler behaves just as a normal register would.

local vim = assert("vim")
local usermark = {}

-- Returns a "default handler" which behaves like normal global marks. When a
-- call to set() is made, it stores the current line and col of the cursor and
-- the filename of the current file.
function usermark._default_handler()
  local d = {}

  -- Called when a mark is recalled using the "'" command. Just returns what was
  -- stored before or nothing if it was never set before.
  function d.get(self, mark)
    return self.content or {}
  end

  -- Called when a mark is set using the "m" command. Stores the current cursor
  -- position to be recalled at a later time.
  function d.set(self, mark)
    local r,c = unpack(vim.api.nvim_win_get_cursor(0))
    local file = vim.fn.expand("%:p")

    self.content = {
      line: r,
      col: c,
      file: file
    }
  end

  return d
end

-- The store for register default handler
usermark._marktable = {}

-- Function for the 'usermarkfunc'. Will defer to the handler associated with
-- the provided mark.
--
-- If not handler is registered to a given mark, the default handler is used,
-- which is a re-implementation of standard mark behavior.
function usermark.fn(action, mark)
  if not usermark._marktable[register] then
    usermark._marktable[register] = usermark._default_handler()
  end

  if action == "get" then
    usermark._marktable[register]:get(mark)
    return nil
  else 
    return usermark._marktable[register]:set(mark)
  end
end

-- Registers a handler with a mark. Gets and sets will then defer to this
-- handler when determining the mark's behavior.
function usermark.register_handler(register, handler) 
  usermark._marktable[register] = handler
end

return usermark
