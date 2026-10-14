local t = require('test.testutil')
local n = require('test.functional.testnvim')()

local eq = t.eq
local clear = n.clear
local feed = n.feed
local eval = n.eval
local fn = n.fn
local dedent = t.dedent

local Screen = require('test.functional.ui.screen')

describe('extregister operations', function()
  local screen
  setup(function()
    clear { arg = { 'runtime/vim/extregs.lua' } }
    screen = Screen.new(50, 10)
    feed([[
      :set rtp+=runtime
      :runtime vim/extregs.lua
      :lua << EOF
      local store = {}
      vim.extregs.register("mem", {
        text_yanked = function(c) store = vim.deepcopy(c) end,
        text_put = function() return store end
      })
      vim.extregs.bind("%", "mem")
      EOF
    ]])
  end)

  describe('yank operations', function()
    it('yanks text into extregister via alias', function()
      feed('iline1<cr>line2<esc>yy%')
      eq('line1\nline2', eval('luaeval("store.lines")'))
    end)

    it('yanks characterwise', function()
      feed('iline1<cr>line2<esc>vw%')
      eq({ lines = { 'line1', 'line2' }, type = 'v' }, eval('luaeval("store")'))
    end)

    it('yanks linewise', function()
      feed('iline1<cr>line2<esc>V%')
      eq({ lines = { 'line1', 'line2' }, type = 'V' }, eval('luaeval("store")'))
    end)
  end)

  describe('delete operations', function()
    setup(function()
      feed('iline1<cr>line2<cr>line3<cr>line4<esc>')
    end)

    it('deletes and commits to extregister', function()
      feed('1dd%')
      eq({ lines = { 'line1' }, type = 'V' }, eval('luaeval("store")'))
    end)

    it('deletes multiple lines', function()
      feed('dd%d%')
      eq({ lines = { 'line1', 'line2' }, type = 'V' }, eval('luaeval("store")'))
    end)
  end)

  describe('put operations', function()
    setup(function()
      feed('iline1<cr>iline2<esc>%')
      feed([[
        :lua << EOF
        store = { lines = { "X1", "X2" }, type = "V" }
        EOF
      ]])
    end)

    it('puts from extregister with alias', function()
      feed('%p')
      screen:expect([[
        ^iline1                      |
        ^X1                          |
        ^X2                          |
        ~                            |
        ~                            |
        ~                            |
        ~                            |
        ~                            |
        ~                            |
        ~                            |
      ]])
    end)

    it('puts from extregister in insert mode', function()
      feed('%p')
      feed('iA')
      screen:expect([[
        ^iline1                      |
        ^X1                          |
        ^X2                          |
        ~                            |
        ~                            |
        ~                            |
        ~                            |
        ~                            |
        ~                            |
        ~                            |
      ]])
    end)
  end)

  describe('getreg()/setreg()', function()
    it('getreg returns extregister contents', function()
      feed([[
        :lua << EOF
        store = { lines = { "hello", "world" }, type = "v" }
        EOF
      ]])
      feed(':let reg = getreg("%")')
      eq('hello world', eval('reg'))
    end)

    it('setreg writes to extregister', function()
      feed(':let @% = "newtext"')
      eq({ lines = { 'newtext' }, type = 'v' }, eval('luaeval("store")'))
    end)

    it('setreg with list and type', function()
      feed([[
        :lua << EOF
        local d = { regcontents = { "a", "b", "c" }, regtype = "V" }
        vim.fn.setreg('%', d)
        EOF
      ]])
      eq({ lines = { 'a', 'b', 'c' }, type = 'V' }, eval('luaeval("store")'))
    end)

    it('setreg with empty dict clears register', function()
      feed([[
        :lua << EOF
        store = { lines = { "existing" }, type = "v" }
        vim.fn.setreg('%', {})
        EOF
      ]])
      eq({ lines = {}, type = 'v' }, eval('luaeval("store")'))
    end)
  end)

  describe('<C-r> operations', function()
    setup(function()
      feed([[
        :lua << EOF
        store = { lines = { "TEXT" }, type = "v" }
        EOF
      ]])
    end)

    it('inserts extregister in insert mode', function()
      feed('ii<C-R>%X')
      screen:expect([[
        i^TEXTX                         |
        ~                               |
        ~                               |
        ~                               |
        ~                               |
        ~                               |
        ~                               |
        ~                               |
        ~                               |
        ~                               |
      ]])
    end)
  end)

  describe(':put command', function()
    it('puts extregister contents', function()
      feed([[
        :lua << EOF
        store = { lines = { "P1", "P2" }, type = "V" }
        EOF
      ]])
      feed(':put %=')
      screen:expect([[
        ~                               |
        ^P1                             |
        ^P2                             |
        ~                               |
        ~                               |
        ~                               |
        ~                               |
        ~                               |
        ~                               |
        ~                               |
      ]])
    end)

    it('puts extregister before cursor', function()
      feed('iline<esc>')
      feed([[
        :lua << EOF
        store = { lines = { "BEFORE" }, type = "V" }
        EOF
      ]])
      feed(':put! %=')
      screen:expect([[
        ^BEFORE                         |
        ^iline                          |
        ~                               |
        ~                               |
        ~                               |
        ~                               |
        ~                               |
        ~                               |
        ~                               |
        ~                               |
      ]])
    end)

    it('iput puts extregister after cursor', function()
      feed('iline<esc>')
      feed([[
        :lua << EOF
        store = { lines = { "AFTER" }, type = "V" }
        EOF
      ]])
      feed(':iput %=')
      screen:expect([[
        ^iline                          |
        ^AFTER                          |
        ~                               |
        ~                               |
        ~                               |
        ~                               |
        ~                               |
        ~                               |
        ~                               |
        ~                               |
      ]])
    end)

    it('puts extregister with bang option', function()
      feed('iline<esc>')
      feed([[
        :lua << EOF
        store = { lines = { "INDENT" }, type = "V" }
        EOF
      ]])
      feed(':put! %=')
      screen:expect([[
        ^INDENT                         |
        ^iline                          |
        ~                               |
        ~                               |
        ~                               |
        ~                               |
        ~                               |
        ~                               |
        ~                               |
        ~                               |
      ]])
    end)
  end)

  describe('named selector', function()
    setup(function()
      feed([[
        :lua << EOF
        vim.extregs.register("first", {
          text_put = function() return { lines = { "FIRST" }, type = "V" } end
        })
        vim.extregs.register("second", {
          text_put = function() return { lines = { "SECOND" }, type = "V" } end
        })
        EOF
      ]])
    end)

    it('prompts for named extregister', function()
      feed('i<C-R>(f')
      screen:expect([[
        ^Extregister: f                  |
        ~                               |
        ~                               |
        ~                               |
        ~                               |
        ~                               |
        ~                               |
        ~                               |
        ~                               |
        ~                               |
      ]])
    end)
  end)
end)
