local userreg = {}

userreg.store = {}
userreg.regtable = {}

function userreg.fn(action, register, content)
  if action == "yank" then
    if userreg.regtable[register] then
      userreg.regtable[register]:do_yank(content)
    else
      userreg.store[register] = content
    end
    return nil
  else 
    if userreg.regtable[register] then
      return userreg.regtable[register]:do_put()
    else
      return userreg.store[register] or {}
    end
  end
end

function userreg.register_handler(register, handler) 
  userreg.regtable[register] = handler
end

return userreg
