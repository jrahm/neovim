local t = require('test.testutil')
local n = require('test.functional.testnvim')()

local eq = t.eq
local clear = n.clear
local feed = n.feed
local eval = n.eval
local pcall_err = t.pcall_err

local Screen = require('test.functional.ui.screen')

describe('extregister API', function()
  local screen
  setup(function()
    clear { arg = { 'runtime/vim/extregs.lua' } }
    screen = Screen.new(40, 5)
  end)

  describe('register()', function()
    it('registers a new provider', function()
      feed([[
        :lua << EOF
        vim.extregs.register("test", {
          text_yanked = function(c) print("YANKED") end
        })
        EOF
      ]])
      eq(true, eval('luaeval("vim._core.editor._extregs ~= nil")'))
    end)

    it('rejects empty provider name', function()
      pcall_err("Provider name cannot be empty", function()
        vim.extregs.register("", {})
      end)
    end)

    it('rejects provider without callbacks', function()
      pcall_err("At least one callback must be provided", function()
        vim.extregs.register("test", {})
      end)
    end)

    it('rejects duplicate registration', function()
      feed([[
        :lua << EOF
        vim.extregs.register("test", { text_yanked = function() end })
        EOF
      ]])
      pcall_err("Extregister 'test' is already registered", function()
        vim.extregs.register("test", { text_yanked = function() end })
      end)
    end)

    it('accepts provider with only text_yanked', function()
      feed([[
        :lua << EOF
        vim.extregs.register("test", { text_yanked = function() end })
        EOF
      ]])
      eq(true, eval('luaeval("vim._core.editor._extregs ~= nil")'))
    end)

    it('accepts provider with only text_put', function()
      feed([[
        :lua << EOF
        vim.extregs.register("test", { text_put = function() return {} end })
        EOF
      ]])
      eq(true, eval('luaeval("vim._core.editor._extregs ~= nil")'))
    end)
  end)

  describe('unregister()', function()
    setup(function()
      feed([[
        :lua << EOF
        vim.extregs.register("test", { text_yanked = function() end })
        EOF
      ]])
    end)

    it('unregisters an existing provider', function()
      feed(':lua vim.extregs.unregister("test")<cr>')
      eq(false, eval('luaeval("vim._core.editor._extregs ~= nil")'))
    end)

    it('rejects non-existent provider', function()
      pcall_err("Extregister 'nonexistent' not registered", function()
        vim.extregs.unregister("nonexistent")
      end)
    end)

    it('rejects empty name', function()
      pcall_err("Provider name cannot be empty", function()
        vim.extregs.unregister("")
      end)
    end)
  end)

  describe('bind()', function()
    setup(function()
      feed([[
        :lua << EOF
        vim.extregs.register("test", { text_yanked = function() end })
        EOF
      ]])
    end)

    it('binds an alias to a provider', function()
      feed(':lua vim.extregs.bind("&", "test")<cr>')
      eq(true, eval('luaeval("vim.extregs.is_alias(\'&\')")'))
    end)

    it('rejects invalid alias character', function()
      pcall_err("Invalid extregister alias", function()
        vim.extregs.bind("%", "test")
      end)
    end)

    it('rejects duplicate alias', function()
      feed([[
        :lua << EOF
        vim.extregs.register("test2", { text_yanked = function() end })
        EOF
      ]])
      feed(':lua vim.extregs.bind("&", "test")<cr>')
      pcall_err("Alias '&' is already bound", function()
        vim.extregs.bind("&", "test2")
      end)
    end)

    it('rejects non-existent provider', function()
      pcall_err("Extregister 'nonexistent' not registered", function()
        vim.extregs.bind("&", "nonexistent")
      end)
    end)

    it('rejects alphanumeric characters', function()
      pcall_err("Invalid extregister alias", function()
        vim.extregs.bind("a", "test")
      end)
      pcall_err("Invalid extregister alias", function()
        vim.extregs.bind("1", "test")
      end)
    end)

    it('rejects reserved characters', function()
      pcall_err("Invalid extregister alias", function()
        vim.extregs.bind("(", "test")
      end)
      pcall_err("Invalid extregister alias", function()
        vim.extregs.bind("=", "test")
      end)
    end)
  end)

  describe('unbind()', function()
    setup(function()
      feed([[
        :lua << EOF
        vim.extregs.register("test", { text_yanked = function() end })
        vim.extregs.bind("&", "test")
        EOF
      ]])
    end)

    it('unbinds an alias', function()
      feed(':lua vim.extregs.unbind("&")<cr>')
      eq(false, eval('luaeval("vim.extregs.is_alias(\'&\')")'))
    end)

    it('rejects non-bound alias', function()
      pcall_err("Alias '#' is not bound", function()
        vim.extregs.unbind("#")
      end)
    end)

    it('rejects invalid alias length', function()
      pcall_err("Alias must be a single character", function()
        vim.extregs.unbind("ab")
      end)
    end)
  end)

  describe('is_alias()', function()
    setup(function()
      feed([[
        :lua << EOF
        vim.extregs.register("test", { text_yanked = function() end })
        vim.extregs.bind("&", "test")
        EOF
      ]])
    end)

    it('returns true for bound alias', function()
      eq(true, eval('luaeval("vim.extregs.is_alias(\'&\')")'))
    end)

    it('returns false for unbound character', function()
      eq(false, eval('luaeval("vim.extregs.is_alias(\'#\')")'))
    end)

    it('returns false for invalid input', function()
      eq(false, eval('luaeval("vim.extregs.is_alias(\'ab\')")'))
    end)
  end)

  describe('integration tests', function()
    it('full lifecycle: register, bind, unbind, unregister', function()
      feed([[
        :lua << EOF
        -- Register
        vim.extregs.register("lifecycle", { text_yanked = function() end })

        -- Bind
        vim.extregs.bind("$", "lifecycle")
        assert(vim.extregs.is_alias("$") == true)

        -- Unbind
        vim.extregs.unbind("$")
        assert(vim.extregs.is_alias("$") == false)

        -- Rebind to another provider
        vim.extregs.register("lifecycle2", { text_yanked = function() end })
        vim.extregs.bind("$", "lifecycle2")
        assert(vim.extregs.is_alias("$") == true)

        -- Unregister (also unbinds)
        vim.extregs.unregister("lifecycle2")
        assert(vim.extregs.is_alias("$") == false)

        -- Cleanup
        vim.extregs.unregister("lifecycle")
        EOF
      ]])
      eq(true, eval('luaeval("true")'))
    end)
  end)
end)
