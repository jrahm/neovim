" This is used for the default userreg function.

lua vim.usermark = require('vim.usermark')

function! userreg#func(action, mark) abort
  return v:lua.vim.usermark.fn(a:action, a:mark)
endfunction
