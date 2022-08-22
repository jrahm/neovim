" This is used for the default userreg function.

lua vim.userreg = require('vim.userreg')

function! userreg#func(action, register, content) abort
  return v:lua.vim.userreg.fn(a:action, a:register, a:content)
endfunction
