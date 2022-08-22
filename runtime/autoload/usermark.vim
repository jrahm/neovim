" This is used for the default userreg function.

lua vim.usermark = require('vim.usermark')

function! usermark#func(action, mark) abort
  let ret = v:lua.vim.usermark.fn(a:action, a:mark)

  echo "Returning: " . string(ret)

  return ret
endfunction
