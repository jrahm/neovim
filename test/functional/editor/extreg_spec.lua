local t = require('test.testutil')
local n = require('test.functional.testnvim')()

local describe, it, before_each = t.describe, t.it, t.before_each
local clear = n.clear
local feed = n.feed
local exec_lua = n.exec_lua
local api = n.api

local eq = t.eq

describe('extregs', function()
  before_each(function()
    clear()

    exec_lua([[
      _G.extreg = nil
      _G.yank_calls = 0
      _G.put_calls = 0

      vim._extregs.register('test',
        function(yankreg)
          _G.yank_calls = _G.yank_calls + 1
          _G.extreg = {
            lines = vim.deepcopy(yankreg.lines),
            size = yankreg.size,
            type = yankreg.type,
          }
          return true
        end,

        function()
          _G.put_calls = _G.put_calls + 1
          return {
            lines = { 'from', 'callback' },
            type = 'V',
          }
        end
      )

      vim._extregs.register('let.test',
        function(yankreg)
          _G.yank_calls = _G.yank_calls + 1
          _G.extreg = {
            lines = vim.deepcopy(yankreg.lines),
            size = yankreg.size,
            type = yankreg.type,
          }
          return true
        end,

        function()
          _G.put_calls = _G.put_calls + 1
          return 'let-test'
        end
      )

      vim._extregs.register('macro.replay',
        function(_)
          return false
        end,

        function()
          _G.put_calls = _G.put_calls + 1
          return 'ggCgoodbye' .. string.char(27)
        end
      )
    ]])
  end)

  it('yanks to and puts from an extreg', function()
    api.nvim_buf_set_lines(0, 0, -1, true, {
      'hello',
      'world',
    })

    feed([[gg"~test<CR>yy]])

    eq(1, exec_lua([[ return _G.yank_calls ]]))
    eq(0, exec_lua([[ return _G.put_calls ]]))

    eq({ 'hello' }, exec_lua([[ return _G.extreg.lines ]]))
    eq(1, exec_lua([[ return _G.extreg.size ]]))
    eq('V', exec_lua([[ return _G.extreg.type ]]))

    feed([[G"~test<CR>p]])

    eq({
      'hello',
      'world',
      'from',
      'callback',
    }, api.nvim_buf_get_lines(0, 0, -1, true))

    eq(1, exec_lua([[ return _G.put_calls ]]))
  end)

  it('supports Vimscript register read/write semantics', function()
    api.nvim_exec2([[let @~let.test = "hi"]], {})

    eq(1, exec_lua([[ return _G.yank_calls ]]))
    eq({ 'hi' }, exec_lua([[ return _G.extreg.lines ]]))
    eq(1, exec_lua([[ return _G.extreg.size ]]))

    api.nvim_exec2([[let x = @~let.test]], {})

    eq(1, exec_lua([[ return _G.put_calls ]]))
    eq('let-test', api.nvim_get_var('x'))
  end)

  it('supports insert-mode put with <C-R>', function()
    api.nvim_exec2([[let @~let.test = "hi"]], {})

    eq(1, exec_lua([[ return _G.yank_calls ]]))
    eq({ 'hi' }, exec_lua([[ return _G.extreg.lines ]]))

    api.nvim_buf_set_lines(0, 0, -1, true, {
      'hello',
      'world',
    })

    feed('gg0lllli<C-R>~let.test<CR><Esc>')

    eq({ 'helllet-testo', 'world' }, api.nvim_buf_get_lines(0, 0, -1, true))
    eq(1, exec_lua([[ return _G.put_calls ]]))
  end)

  it('supports macro recording', function()
    feed('q~test<CR>jjjjjkkkkkq')

    eq(1, exec_lua([[ return _G.yank_calls ]]))
    eq({ 'jjjjjkkkkk' }, exec_lua([[ return _G.extreg.lines ]]))
  end)

  it('supports macro replaying', function()
    api.nvim_buf_set_lines(0, 0, -1, true, {
      'hello',
      'world',
    })

    feed('@~macro.replay<CR>')

    eq(1, exec_lua([[ return _G.put_calls ]]))
    eq({ 'goodbye', 'world' }, api.nvim_buf_get_lines(0, 0, -1, true))
  end)

  it('supports :put', function()
    feed(':put ~let.test<CR>')

    eq(1, exec_lua([[ return _G.put_calls ]]))
    eq({ '', 'let-test' }, api.nvim_buf_get_lines(0, 0, -1, true))
  end)

  it('supports cmdline mode <C-R>', function()
    exec_lua([[
      vim._extregs.register('cmdline.test',
        function(_)
          return false
        end,

        function()
          _G.put_calls = _G.put_calls + 1
          return '0put ~let.test'
        end
      )
    ]])

    feed(':<C-R>~cmdline.test<CR><CR>')

    eq(2, exec_lua([[ return _G.put_calls ]]))
    eq({ 'let-test', '' }, api.nvim_buf_get_lines(0, 0, -1, true))
  end)

  it('supports setreg() and getreg()', function()
    exec_lua([[
      vim._extregs.register('func.test',
        function(yankreg)
          _G.yank_calls = _G.yank_calls + 1
          _G.extreg = {
            lines = vim.deepcopy(yankreg.lines),
            size = yankreg.size,
            type = yankreg.type,
          }
          return true
        end,

        function()
          _G.put_calls = _G.put_calls + 1
          return {
            lines = { 'from getreg' },
            type = 'v',
          }
        end
      )
    ]])

    exec_lua([[
      vim.fn.setreg('~func.test', 'hi')
    ]])

    eq(1, exec_lua([[ return _G.yank_calls ]]))
    eq({ 'hi' }, exec_lua([[ return _G.extreg.lines ]]))
    eq(1, exec_lua([[ return _G.extreg.size ]]))

    eq('from getreg', exec_lua([[
      return vim.fn.getreg('~func.test')
    ]]))

    eq(1, exec_lua([[ return _G.put_calls ]]))
  end)

  it('supports visual-mode put', function()
    api.nvim_buf_set_lines(0, 0, -1, true, {
      'hello world',
    })

    feed('gg0vllll"~let.test<CR>p')

    eq(1, exec_lua([[ return _G.put_calls ]]))
    eq({ 'let-test world' }, api.nvim_buf_get_lines(0, 0, -1, true))
  end)

  it('supports linewise DELETE', function()
    api.nvim_buf_set_lines(0, 0, -1, true, {
      'hello',
      'world',
      'foo',
      'bar',
    })

    feed('gg"~test<CR>dd')

    eq(1, exec_lua([[ return _G.yank_calls ]]))
    eq({ 'hello' }, exec_lua([[ return _G.extreg.lines ]]))
    eq(1, exec_lua([[ return _G.extreg.size ]]))
    eq('V', exec_lua([[ return _G.extreg.type ]]))

    eq({
      'world',
      'foo',
      'bar',
    }, api.nvim_buf_get_lines(0, 0, -1, true))
  end)

  it('supports characterwise DELETE motion', function()
    api.nvim_buf_set_lines(0, 0, -1, true, {
      'hello goodbye world!',
    })

    feed('ggwll"~test<CR>daw')

    eq(1, exec_lua([[ return _G.yank_calls ]]))
    eq({ 'goodbye ' }, exec_lua([[ return _G.extreg.lines ]]))
    eq('v', exec_lua([[ return _G.extreg.type ]]))

    eq({
      'hello world!',
    }, api.nvim_buf_get_lines(0, 0, -1, true))
  end)

  it('supports characterwise put', function()
    exec_lua([[
      vim._extregs.register('char.test',
        function(_)
          return false
        end,

        function()
          return {
            lines = { 'XYZ' },
            type = 'v',
          }
        end
      )
    ]])

    api.nvim_buf_set_lines(0, 0, -1, true, {
      'hello',
    })

    feed('gg0ll"~char.test<CR>p')

    eq({ 'helXYZlo' }, api.nvim_buf_get_lines(0, 0, -1, true))
  end)

  it('supports multiline characterwise put', function()
    exec_lua([[
      vim._extregs.register('char.multi',
        function(_)
          return false
        end,

        function()
          return {
            lines = { 'AAA', 'BBB' },
            type = 'v',
          }
        end
      )
    ]])

    api.nvim_buf_set_lines(0, 0, -1, true, {
      'hello world',
    })

    feed('gg0llll"~char.multi<CR>p')

    eq({
      'helloAAA',
      'BBB world',
    }, api.nvim_buf_get_lines(0, 0, -1, true))
  end)

  it('supports blockwise yank', function()
    api.nvim_buf_set_lines(0, 0, -1, true, {
      'abcde',
      'ABCDE',
    })

    feed('gg0<C-V>jll"~test<CR>y')

    eq(1, exec_lua([[ return _G.yank_calls ]]))
    eq({ 'abc', 'ABC' }, exec_lua([[ return _G.extreg.lines ]]))

    local regtype = exec_lua([[ return _G.extreg.type ]])
    eq("b", regtype)
  end)

  it('supports blockwise put', function()
    exec_lua([[
      vim._extregs.register('block.test',
        function(_)
          return false
        end,

        function()
          return {
            lines = { 'XX', 'YY' },
            type = 'b',
          }
        end
      )
    ]])

    api.nvim_buf_set_lines(0, 0, -1, true, {
      'aaaa',
      'bbbb',
    })

    feed('gg0"~block.test<CR>p')

    eq({
      'aXXaaa',
      'bYYbbb',
    }, api.nvim_buf_get_lines(0, 0, -1, true))
  end)

  it('writes a line delete to the extreg once while preserving delete history', function()
    api.nvim_buf_set_lines(0, 0, -1, true, {
      'hello',
      'world',
    })

    feed('gg"~test<CR>dd')

    eq(1, exec_lua([[ return _G.yank_calls ]]))
    eq({ 'hello' }, exec_lua([[ return _G.extreg.lines ]]))

    eq('hello\n', exec_lua([[
      return vim.fn.getreg('1')
    ]]))

    eq({ 'world' }, api.nvim_buf_get_lines(0, 0, -1, true))
  end)

  it('preserves small-delete register semantics', function()
    exec_lua([[
      vim.fn.setreg('-', 'sentinel')
    ]])

    api.nvim_buf_set_lines(0, 0, -1, true, {
      'hello world',
    })

    feed('gg0"~test<CR>dw')

    eq(1, exec_lua([[ return _G.yank_calls ]]))
    eq({ 'hello ' }, exec_lua([[ return _G.extreg.lines ]]))

    -- An explicit register suppresses the small-delete register.
    eq('sentinel', exec_lua([[
      return vim.fn.getreg('-')
    ]]))

    eq({ 'world' }, api.nvim_buf_get_lines(0, 0, -1, true))
  end)

  it('supports getreg() list semantics', function()
    exec_lua([[
      vim._extregs.register('list.test',
        function(_)
          return false
        end,

        function()
          return {
            lines = { 'one', 'two', 'three' },
            type = 'V',
          }
        end
      )
    ]])

    eq({ 'one', 'two', 'three' }, exec_lua([[
      return vim.fn.getreg('~list.test', 1, true)
    ]]))
  end)

  it('supports setreg() with a list', function()
    exec_lua([[
      vim.fn.setreg('~test', { 'one', 'two', 'three' }, 'V')
    ]])

    eq(1, exec_lua([[ return _G.yank_calls ]]))

    eq({
      'one',
      'two',
      'three',
    }, exec_lua([[ return _G.extreg.lines ]]))

    eq(3, exec_lua([[ return _G.extreg.size ]]))
    eq('V', exec_lua([[ return _G.extreg.type ]]))
  end)

  it('supports unregistering and re-registering an extreg provider', function()
    exec_lua([[
      vim._extregs.register('replace.test',
        function(_) return false end,
        function() return 'old' end
      )

      vim._extregs.unregister('replace.test')

      vim._extregs.register('replace.test',
        function(_) return false end,
        function() return 'new' end
      )
    ]])

    eq('new', exec_lua([[
      return vim.fn.getreg('~replace.test')
    ]]))
  end)

  it('rejects duplicate extreg registration', function()
    exec_lua([[
      vim.cmd("let v:errmsg = ''")

      vim._extregs.register('test',
        function(_) return false end,
        function() return 'duplicate' end
      )
    ]])

    eq(true, exec_lua([[
      return vim.v.errmsg:find('E356:', 1, true) ~= nil
    ]]))

    -- The original provider must remain installed.
    eq('from\ncallback\n', exec_lua([[
      return vim.fn.getreg('~test')
    ]]))
  end)

  it('supports multiline string return values', function()
    exec_lua([[
      vim._extregs.register('string.multi',
        function(_)
          return false
        end,

        function()
          return 'one\ntwo\nthree'
        end
      )
    ]])

    eq('one\ntwo\nthree', exec_lua([[
      return vim.fn.getreg('~string.multi')
    ]]))
  end)

  it('does not leak extreg state into later ordinary register operations', function()
    api.nvim_buf_set_lines(0, 0, -1, true, {
      'one',
      'two',
      'three',
    })

    feed('gg"~test<CR>yy')

    eq(1, exec_lua([[ return _G.yank_calls ]]))

    feed('j"ayy')

    eq(1, exec_lua([[ return _G.yank_calls ]]))

    eq('two\n', exec_lua([[
      return vim.fn.getreg('a')
    ]]))
  end)

  it('handles a yank callback returning false without poisoning state', function()
    exec_lua([[
      _G.false_yank_calls = 0

      vim._extregs.register('returns.false.yank',
        function(_)
          _G.false_yank_calls = _G.false_yank_calls + 1
          return false
        end,

        function()
          return 'unused'
        end
      )
    ]])

    api.nvim_buf_set_lines(0, 0, -1, true, {
      'hello',
      'world',
    })

    feed('gg"~returns.false.yank<CR>yiw')

    eq(1, exec_lua([[ return _G.false_yank_calls ]]))

    -- A yank must not have changed the buffer.
    eq({
      'hello',
      'world',
    }, api.nvim_buf_get_lines(0, 0, -1, true))

    -- Failure must not leave stale extreg state behind.
    exec_lua([[
      vim.fn.setreg('~let.test', 'after false')
    ]])

    eq(1, exec_lua([[ return _G.yank_calls ]]))
    eq({ 'after false' }, exec_lua([[ return _G.extreg.lines ]]))
  end)

  it('handles errors in yank callbacks', function()
    exec_lua([[
      vim._extregs.register('crash.yank',
        function(_)
          error('yank exploded')
        end,

        function()
          return 'unused'
        end
      )
    ]])

    local result = exec_lua([[
      local ok, err = pcall(vim.fn.setreg, '~crash.yank', 'hello')
      return { ok, tostring(err) }
    ]])

    eq(false, result[1])
    eq(true, result[2]:find('yank exploded', 1, true) ~= nil)

    -- The error must not poison subsequent extreg operations.
    exec_lua([[
      vim.fn.setreg('~let.test', 'after crash')
    ]])

    eq(1, exec_lua([[ return _G.yank_calls ]]))
    eq({ 'after crash' }, exec_lua([[ return _G.extreg.lines ]]))
  end)

  it('handles errors in put callbacks', function()
    exec_lua([[
      vim._extregs.register('crash.put',
        function(_)
          return true
        end,

        function()
          error('put exploded')
        end
      )
    ]])

    local result = exec_lua([[
      local ok, value = pcall(vim.fn.getreg, '~crash.put')
      return { ok, tostring(value) }
    ]])

    eq(false, result[1])
    eq(true, result[2]:find('put exploded', 1, true) ~= nil)

    -- The error must not poison subsequent extreg operations.
    eq('let-test', exec_lua([[
      return vim.fn.getreg('~let.test')
    ]]))

    eq(1, exec_lua([[ return _G.put_calls ]]))
  end)

  it('alias use followed by ordinary register does not leak active_extreg', function()
    -- Setup: register an extreg provider that tracks yank calls
    exec_lua([[
      _G.yank_calls = 0
      vim._extregs.register('test', function()
        _G.yank_calls = _G.yank_calls + 1
        return { lines = { 'from-extreg' }, size = 1, type = 'v' }
      end, function(data)
        return data.lines[1]
      end)
      vim._extregs.bind('~', 'test')
    ]])

    -- Set up buffer content
    api.nvim_buf_set_lines(0, 0, -1, true, { 'hello', 'world' })

    -- Use the extreg alias (should trigger yank callback)
    feed([[gg"~test<CR>yy]])

    eq(1, exec_lua([[ return _G.yank_calls ]]))

    -- Now use an ordinary register - extreg should NOT be called again
    feed([[gg"a<CR>yy]])
    eq(1, exec_lua([[ return _G.yank_calls ]])) -- Still 1, not 2!

    -- Cleanup
    exec_lua([[
      vim._extregs.unbind('~')
      vim._extregs.unregister('test')
    ]])
  end)

  it('displays extregs in :registers command without materializing them', function()
    exec_lua([[
      _G.yank_calls = 0
      _G.put_calls = 0

      vim._extregs.register(
        'test',
        function(_)
          _G.yank_calls = _G.yank_calls + 1
          return true
        end,
        function()
          _G.put_calls = _G.put_calls + 1
          return {
            lines = { 'a', 'b', 'c' },
            size = 3,
            type = 'v',
          }
        end
      )
    ]])

    local msg = api.nvim_cmd({ cmd = 'registers' }, { output = true })

    eq(true, msg:find('\nExtregs\n', 1, true) ~= nil)
    eq(true, msg:find('\n  c  "~test\n', 1, true) ~= nil)

    eq(
      { yank_calls = 0, put_calls = 0 },
      exec_lua([[
        return {
          yank_calls = _G.yank_calls,
          put_calls = _G.put_calls,
        }
      ]])
    )

    exec_lua([[
      vim._extregs.unregister('test')
      _G.yank_calls = nil
      _G.put_calls = nil
    ]])
  end)

  it('displays extreg aliases in :registers command', function()
    exec_lua([[
      _G.yank_calls = 0
      _G.put_calls = 0

      vim._extregs.register(
        'test',
        function(_)
          _G.yank_calls = _G.yank_calls + 1
          return true
        end,
        function()
          _G.put_calls = _G.put_calls + 1
          return {
            lines = { 'content' },
            size = 1,
            type = 'v',
          }
        end
      )

      vim._extregs.bind('&', 'test')
    ]])

    local msg = api.nvim_cmd({ cmd = 'registers' }, { output = true })

    eq(true, msg:find('\nExtregs\n', 1, true) ~= nil)
    eq(true, msg:find('\n  c  "~test\n', 1, true) ~= nil)

    eq(true, msg:find('\nAliases\n', 1, true) ~= nil)
    eq(true, msg:find('\n  *  "& -> "~test', 1, true) ~= nil)

    eq(
      { yank_calls = 0, put_calls = 0 },
      exec_lua([[
        return {
          yank_calls = _G.yank_calls,
          put_calls = _G.put_calls,
        }
      ]])
    )

    exec_lua([[
      vim._extregs.unbind('&')
      vim._extregs.unregister('test')
      _G.yank_calls = nil
      _G.put_calls = nil
    ]])
  end)

  it('displays multiple extregs in :registers command', function()
    exec_lua([[
      vim._extregs.register(
        'alpha',
        function(_) return true end,
        function() return 'alpha-content' end
      )

      vim._extregs.register(
        'beta',
        function(_) return true end,
        function() return 'beta-content' end
      )
    ]])

    local msg = api.nvim_cmd({ cmd = 'registers' }, { output = true })

    eq(true, msg:find('\nExtregs\n', 1, true) ~= nil)
    eq(true, msg:find('\n  c  "~alpha\n', 1, true) ~= nil)
    eq(true, msg:find('\n  c  "~beta\n', 1, true) ~= nil)

    exec_lua([[
      vim._extregs.unregister('alpha')
      vim._extregs.unregister('beta')
    ]])
  end)
  it('sets v:register for extreg yank and put', function()
    exec_lua([[
      _G.yank_v_register = nil
      _G.put_v_register = nil

      vim._extregs.register('vreg.test',
        function(yankreg)
          _G.yank_v_register = vim.v.register
          return true
        end,

        function()
          _G.put_v_register = vim.v.register
          return {
            lines = { 'from-vreg' },
            type = 'v',
          }
        end
      )
    ]])

    api.nvim_buf_set_lines(0, 0, -1, true, {
      'hello',
    })

    feed('gg"~vreg.test<CR>yy')

    eq('~vreg.test', exec_lua([[
      return _G.yank_v_register
    ]]))

    feed('gg"~vreg.test<CR>p')

    eq('~vreg.test', exec_lua([[
      return _G.put_v_register
    ]]))
  end)

  it('dot-repeat calls extreg put callback each time', function()
    exec_lua([[
      _G.put_counter = 0
      vim._extregs.register('counter',
        function(_)
          return true
        end,
        function()
          _G.put_counter = _G.put_counter + 1
          return {
            lines = { 'counter: ' .. _G.put_counter },
            size = 1,
            type = 'V',
          }
        end
      )
    ]])

    api.nvim_buf_set_lines(0, 0, -1, true, {
      'line one',
      'line two',
      'line three',
      'line four',
      'line five',
    })

    -- Perform initial linewise put with extreg at end of file
    feed('G"~counter<CR>p')

    eq(1, exec_lua([[ return _G.put_counter ]]))

    -- First dot-repeat
    feed('.')

    -- Extreg callback should be called again for dot-repeat
    eq(2, exec_lua([[ return _G.put_counter ]]))

    -- Second dot-repeat
    feed('.')

    eq(3, exec_lua([[ return _G.put_counter ]]))

    -- Third dot-repeat
    feed('.')

    eq(4, exec_lua([[ return _G.put_counter ]]))

    -- Verify that content was actually put each time (linewise puts after cursor)
    eq({
      'line one',
      'line two',
      'line three',
      'line four',
      'line five',
      'counter: 1',
      'counter: 2',
      'counter: 3',
      'counter: 4',
    }, api.nvim_buf_get_lines(0, 0, -1, true))

    -- Cleanup
    exec_lua([[
      vim._extregs.unregister('counter')
      _G.put_counter = nil
    ]])
  end)

  it('properly displays yanked status line', function()
    exec_lua([[
      vim._extregs.register('empty.reg',
        function(_)
          return true
        end,
        -1
      )
    ]])

    api.nvim_buf_set_lines(0, 0, -1, true, {
      'line one',
      'line two',
      'line three',
      'line four',
      'line five',
    })

    -- Perform initial linewise put with extreg at end of file
    feed(':%yank ~empty.reg<CR>')
    eq('5 lines yanked into "~empty.reg', exec_lua([[ return vim.v.statusmsg ]]))
  end)
end)
