-- Defualt implementation of the usermarkfunc. This default implementation is
-- extensible and allows other plugins to register handlers for different
-- registers.
--
-- The default handler behaves just as a normal register would.

local vim = assert(vim)
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
      line = r;
      col = c;
    }

    if file ~= '' then
      self.content.file = file
    end
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
  if not usermark._marktable[mark] then
    usermark._marktable[mark] = usermark._default_handler()
  end

  if action == "get" then
    return usermark._marktable[mark]:get(mark)
  else 
    usermark._marktable[mark]:set(mark)
    return nil
  end
end

-- Registers a handler with a mark. Gets and sets will then defer to this
-- handler when determining the mark's behavior.
function usermark.register_handler(mark, handler) 
  usermark._marktable[mark] = handler
end

return usermark
