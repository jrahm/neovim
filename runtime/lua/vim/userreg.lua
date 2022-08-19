-- Defualt implementation of the userregfunc. This default implementation is
-- extensible and allows other plugins to register handlers for different
-- registers.
--
-- The default handler behaves just as a normal register would.

local userreg = {}

-- Returns a "default handler" which behaves exactly like the builtin registers
-- in Vim. Simply stores whatever was yanked and returns the last thing that was
-- yanked.
function userreg._default_handler()
  local d = {}

  function d.do_yank(self, content)
    self.content = content
  end

  function d.do_put(self)
    return self.content or {}
  end

  return d
end

-- The store for registers default handler
userreg._regtable = {}

-- Function for the userreg. This function will defer to the handler registered
-- to the given register. If no handler is registered to the given register, the
-- default handler is used.
function userreg.fn(action, register, content)
  if not userreg._regtable[register] then
    userreg._regtable[register] = userreg._default_handler()
  end

  if action == "yank" then
    userreg._regtable[register]:do_yank(content)
    return nil
  else 
    return userreg._regtable[register]:do_put()
  end
end

-- Registers a handler with a register. Future yanks and puts will defer to the
-- handler when determining the content to put/yank.
function userreg.register_handler(register, handler) 
  userreg._regtable[register] = handler
end

return userreg
