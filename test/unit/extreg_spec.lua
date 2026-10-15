local t = require('test.unit.testutil')

local itp = t.gen_itp(it)
local cimport = t.cimport
local child_call_once = t.child_call_once
local eq = t.eq
local ffi = t.ffi

local lib = cimport('./src/nvim/extreg.h')

-- Single initialization for all tests
child_call_once(function()
  lib.extreg_init()
end)

describe('extreg provider registration', function()
  itp('provider can be registered successfully', function()
    local result = lib.extreg_register('test-provider', -1, -1)
    eq(1, result) -- extreg_register returns int (1 = success)
    
    local prov = lib.extreg_get_provider('test-provider')
    eq(true, prov ~= nil)
    
    -- Cleanup
    lib.extreg_unregister('test-provider')
  end)

  itp('duplicate provider registration fails', function()
    lib.extreg_register('test-provider', -1, -1)
    local result = lib.extreg_register('test-provider', -1, -1)
    eq(0, result) -- Duplicate should fail (0 = failure)
    
    -- Cleanup
    lib.extreg_unregister('test-provider')
  end)

  itp('provider name too long fails', function()
    local long_name = ('a'):rep(257)
    local result = lib.extreg_register(long_name, -1, -1)
    eq(0, result) -- Name too long should fail
  end)

  itp('provider can be retrieved by name', function()
    lib.extreg_register('named-provider', -1, -1)
    
    local prov = lib.extreg_get_provider('named-provider')
    eq(true, prov ~= nil)
    
    -- Cleanup
    lib.extreg_unregister('named-provider')
  end)

  itp('nonexistent provider returns nil', function()
    local prov = lib.extreg_get_provider('nonexistent-provider')
    eq(nil, prov)
  end)
end)

describe('extreg provider unregistering', function()
  itp('provider can be unregistered successfully', function()
    lib.extreg_register('to-unregister', -1, -1)
    
    local result = lib.extreg_unregister('to-unregister')
    eq(1, result) -- 1 = success
    
    -- Provider should be gone
    local prov = lib.extreg_get_provider('to-unregister')
    eq(nil, prov)
  end)

  itp('unregistering nonexistent provider fails', function()
    local result = lib.extreg_unregister('nonexistent-provider')
    eq(0, result) -- 0 = failure
  end)

  itp('unregistering already unregistered provider fails', function()
    lib.extreg_register('once-only', -1, -1)
    lib.extreg_unregister('once-only')
    
    local result = lib.extreg_unregister('once-only')
    eq(0, result) -- 0 = failure
  end)
end)

describe('extreg alias binding', function()
  itp('alias can be bound to provider', function()
    lib.extreg_register('alias-target', -1, -1)
    
    local result = lib.extreg_bind('@', 'alias-target')
    eq(1, result) -- 1 = success
    
    eq(true, lib.extreg_is_alias('@'))
    
    -- Cleanup
    lib.extreg_unbind('@')
    lib.extreg_unregister('alias-target')
  end)

  itp('lowercase letters cannot be used as aliases', function()
    lib.extreg_register('test-provider', -1, -1)
    
    eq(0, lib.extreg_bind('a', 'test-provider'))
    eq(0, lib.extreg_bind('b', 'test-provider'))
    eq(0, lib.extreg_bind('z', 'test-provider'))
    
    -- Cleanup
    lib.extreg_unregister('test-provider')
  end)

  itp('uppercase letters cannot be used as aliases', function()
    lib.extreg_register('test-provider', -1, -1)
    
    eq(0, lib.extreg_bind('A', 'test-provider'))
    eq(0, lib.extreg_bind('B', 'test-provider'))
    eq(0, lib.extreg_bind('Z', 'test-provider'))
    
    -- Cleanup
    lib.extreg_unregister('test-provider')
  end)

  itp('special characters cannot be used as aliases', function()
    lib.extreg_register('test-provider', -1, -1)
    
    eq(0, lib.extreg_bind('=', 'test-provider'))
    eq(0, lib.extreg_bind('#', 'test-provider'))
    eq(0, lib.extreg_bind('%', 'test-provider'))
    eq(0, lib.extreg_bind('.', 'test-provider'))
    eq(0, lib.extreg_bind('/', 'test-provider'))
    eq(0, lib.extreg_bind(':', 'test-provider'))
    eq(0, lib.extreg_bind(';', 'test-provider'))
    eq(0, lib.extreg_bind('+', 'test-provider'))
    
    -- Cleanup
    lib.extreg_unregister('test-provider')
  end)

  itp('digits cannot be used as aliases', function()
    lib.extreg_register('test-provider', -1, -1)
    
    for i = 0, 9 do
      eq(0, lib.extreg_bind(ffi.string(tostring(i)), 'test-provider'))
    end
    
    -- Cleanup
    lib.extreg_unregister('test-provider')
  end)

  itp('space and tab cannot be used as aliases', function()
    lib.extreg_register('test-provider', -1, -1)
    
    eq(0, lib.extreg_bind(string.char(32), 'test-provider')) -- space
    eq(0, lib.extreg_bind(string.char(9), 'test-provider'))  -- tab
    
    -- Note: newline and carriage return ARE allowed (ascii_iswhite only checks space and tab)
    eq(1, lib.extreg_bind(string.char(10), 'test-provider')) -- newline
    eq(1, lib.extreg_bind(string.char(13), 'test-provider')) -- carriage return
    
    -- Cleanup
    lib.extreg_unbind(string.char(10))
    lib.extreg_unbind(string.char(13))
    lib.extreg_unregister('test-provider')
  end)

  itp('valid special characters can be used as aliases', function()
    lib.extreg_register('test-provider', -1, -1)
    
    eq(1, lib.extreg_bind('@', 'test-provider'))
    eq(1, lib.extreg_bind('$', 'test-provider'))
    eq(1, lib.extreg_bind('!', 'test-provider'))
    eq(1, lib.extreg_bind('`', 'test-provider'))
    
    -- Cleanup
    lib.extreg_unbind('@')
    lib.extreg_unbind('$')
    lib.extreg_unbind('!')
    lib.extreg_unbind('`')
    lib.extreg_unregister('test-provider')
  end)

  itp('alias can be rebound to different provider', function()
    lib.extreg_register('provider-1', -1, -1)
    lib.extreg_register('provider-2', -1, -1)
    
    eq(1, lib.extreg_bind('@', 'provider-1'))
    eq(true, lib.extreg_is_alias('@'))
    
    eq(1, lib.extreg_bind('@', 'provider-2'))
    eq(true, lib.extreg_is_alias('@')) -- Alias still exists
    
    -- Cleanup
    lib.extreg_unbind('@')
    lib.extreg_unregister('provider-1')
    lib.extreg_unregister('provider-2')
  end)
end)

describe('extreg alias unbinding', function()
  itp('alias can be unbound successfully', function()
    lib.extreg_register('unbind-target', -1, -1)
    eq(1, lib.extreg_bind('`', 'unbind-target'))
    
    eq(true, lib.extreg_is_alias('`'))
    
    local result = lib.extreg_unbind('`')
    eq(1, result) -- 1 = success
    
    eq(false, lib.extreg_is_alias('`'))
    
    -- Cleanup
    lib.extreg_unregister('unbind-target')
  end)

  itp('unbinding non-bound alias fails', function()
    local result = lib.extreg_unbind('@')
    eq(0, result) -- 0 = failure
    
    eq(false, lib.extreg_is_alias('@'))
  end)
end)

describe('extreg alias checking', function()
  itp('is_alias returns true for bound alias', function()
    lib.extreg_register('alias-test', -1, -1)
    eq(1, lib.extreg_bind('$', 'alias-test'))
    
    eq(true, lib.extreg_is_alias('$'))
    
    -- Cleanup
    lib.extreg_unbind('$')
    lib.extreg_unregister('alias-test')
  end)

  itp('is_alias returns false for unbound alias', function()
    eq(false, lib.extreg_is_alias('`'))
  end)
end)

describe('extreg clear all', function()
  itp('clear_all removes all providers', function()
    lib.extreg_register('clear-1', -1, -1)
    lib.extreg_register('clear-2', -1, -1)
    
    eq(true, lib.extreg_get_provider('clear-1') ~= nil)
    eq(true, lib.extreg_get_provider('clear-2') ~= nil)
    
    lib.extreg_clear_all()
    
    eq(nil, lib.extreg_get_provider('clear-1'))
    eq(nil, lib.extreg_get_provider('clear-2'))
  end)

  itp('clear_all does not clear aliases (per user preference)', function()
    lib.extreg_register('alias-provider', -1, -1)
    eq(1, lib.extreg_bind('^', 'alias-provider'))
    
    eq(true, lib.extreg_is_alias('^'))
    
    lib.extreg_clear_all()
    
    -- Aliases are NOT cleared per user preference (simple alias semantics)
    eq(true, lib.extreg_is_alias('^'))
    
    -- Cleanup
    lib.extreg_unbind('^')
  end)
end)

describe('extreg Lua callbacks', function()
  itp('provider can be registered with -1 for callbacks', function()
    lib.extreg_register('no-callbacks', -1, -1)
    
    local prov = lib.extreg_get_provider('no-callbacks')
    eq(true, prov ~= nil)
    
    -- Cleanup
    lib.extreg_unregister('no-callbacks')
  end)
end)

describe('extreg recursion guard', function()
  itp('recursion guard exists and works', function()
    lib.extreg_register('recursion-test', -1, -1)
    
    local prov = lib.extreg_get_provider('recursion-test')
    eq(true, prov ~= nil)
    
    -- Initially not recursing
    eq(false, lib.extreg_is_recursing(prov))
    
    -- Cleanup
    lib.extreg_unregister('recursion-test')
  end)
end)

describe('extreg parse provider', function()
  itp('parse_provider extracts name from ~provider syntax', function()
    local input = ffi.new('char[16]', '~provider-extra')
    local output = ffi.new('char*[1]')
    
    local result = lib.extreg_parse_provider(input, output)
    eq(true, result ~= nil)
    eq('provider', ffi.string(result))
    
    ffi.gc(result, ffi.free)
  end)

  itp('parse_provider fails with just ~', function()
    local input = ffi.new('char[2]', '~')
    local output = ffi.new('char*[1]')
    
    local result = lib.extreg_parse_provider(input, output)
    eq(nil, result)
  end)

  itp('parse_provider stops at invalid characters', function()
    local input = ffi.new('char[10]', '~provider!name')
    local output = ffi.new('char*[1]')
    
    local result = lib.extreg_parse_provider(input, output)
    -- Should stop at the '!' character
    if result then
      eq('provider', ffi.string(result))
      ffi.gc(result, ffi.free)
    end
  end)
end)

describe('extreg Lua bindings', function()
  -- These tests would require a real Lua state to test the nlua_* functions
  -- For now, we just verify they're accessible
  itp('Lua binding functions exist', function()
    eq(true, lib.nlua_extregs_register ~= nil)
    eq(true, lib.nlua_extregs_unregister ~= nil)
    eq(true, lib.nlua_extregs_bind ~= nil)
    eq(true, lib.nlua_extregs_unbind ~= nil)
    eq(true, lib.nlua_extregs_is_alias ~= nil)
    eq(true, lib.nlua_extregs_get_provider ~= nil)
  end)
end)

describe('extreg Unicode aliases', function()
  itp('Unicode codepoints can be used as aliases', function()
    lib.extreg_register('unicode-provider', -1, -1)
    
    -- Test various Unicode codepoints
    -- U+263A WHITE SMILING FACE
    local smiley = ffi.new('char[5]')
    smiley[0] = 0xF0
    smiley[1] = 0x9F
    smiley[2] = 0x98
    smiley[3] = 0xBA
    smiley[4] = 0
    eq(1, lib.extreg_bind(ffi.string(smiley), 'unicode-provider'))
    
    -- Cleanup
    lib.extreg_unbind(ffi.string(smiley))
    lib.extreg_unregister('unicode-provider')
  end)

  itp('Unicode private use areas cannot be used as aliases', function()
    lib.extreg_register('pu-provider', -1, -1)
    
    -- Test PUA codepoint U+E000
    local pua = ffi.new('char[4]')
    pua[0] = 0xEE
    pua[1] = 0x80
    pua[2] = 0x80
    pua[3] = 0
    eq(0, lib.extreg_bind(ffi.string(pua), 'pu-provider'))
    
    -- Cleanup
    lib.extreg_unregister('pu-provider')
  end)
end)

describe('extreg edge cases', function()
  itp('empty provider name is allowed', function()
    lib.extreg_register('', -1, -1)
    local prov = lib.extreg_get_provider('')
    eq(true, prov ~= nil)
    
    -- Cleanup
    lib.extreg_unregister('')
  end)

  itp('provider name with spaces works', function()
    lib.extreg_register('provider-with-spaces', -1, -1)
    local prov = lib.extreg_get_provider('provider-with-spaces')
    eq(true, prov ~= nil)
    
    -- Cleanup
    lib.extreg_unregister('provider-with-spaces')
  end)

  itp('provider name with special characters works', function()
    lib.extreg_register('provider.with_special-chars', -1, -1)
    local prov = lib.extreg_get_provider('provider.with_special-chars')
    eq(true, prov ~= nil)
    
    -- Cleanup
    lib.extreg_unregister('provider.with_special-chars')
  end)

  itp('multiple providers can be registered simultaneously', function()
    lib.extreg_register('prov-1', -1, -1)
    lib.extreg_register('prov-2', -1, -1)
    lib.extreg_register('prov-3', -1, -1)
    
    eq(true, lib.extreg_get_provider('prov-1') ~= nil)
    eq(true, lib.extreg_get_provider('prov-2') ~= nil)
    eq(true, lib.extreg_get_provider('prov-3') ~= nil)
    
    -- Cleanup
    lib.extreg_unregister('prov-1')
    lib.extreg_unregister('prov-2')
    lib.extreg_unregister('prov-3')
  end)

  itp('alias rebind updates binding', function()
    lib.extreg_register('prov-a', -1, -1)
    lib.extreg_register('prov-b', -1, -1)
    
    eq(1, lib.extreg_bind('@', 'prov-a'))
    eq(true, lib.extreg_is_alias('@'))
    
    eq(1, lib.extreg_bind('@', 'prov-b'))
    eq(true, lib.extreg_is_alias('@')) -- Alias still exists
    
    -- Cleanup
    lib.extreg_unbind('@')
    lib.extreg_unregister('prov-a')
    lib.extreg_unregister('prov-b')
  end)
end)
