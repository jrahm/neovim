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
  lib.extreg_register('provider-a', -1, -1)
  lib.extreg_register('provider-b', -1, -1)
end)

describe('extreg steel thread', function()
  -- Core functionality tests that demonstrate the steel thread
  itp('provider can be registered and retrieved by name', function()
    local prov = lib.extreg_get_provider('provider-a')
    eq(true, prov ~= nil)
  end)

  itp('provider can be retrieved by ID', function()
    local prov = lib.extreg_get_provider_by_id(1)
    eq(true, prov ~= nil)
  end)

  itp('alias can be unbound', function()
    lib.extreg_bind('^', 'provider-a')
    eq(true, lib.extreg_is_alias('^'))
    lib.extreg_unbind('^')
    eq(true, not lib.extreg_is_alias('^'))
  end)

  itp('provider ID can be retrieved from alias', function()
    lib.extreg_bind('`', 'provider-a')
    local prov_id = lib.extreg_get_provider_id('`')
    eq(1, prov_id)  -- provider-a has ID 1
  end)

  itp('invalid provider ID returns nil', function()
    eq(nil, lib.extreg_get_provider_by_id(0))
    eq(nil, lib.extreg_get_provider_by_id(-1))
    eq(nil, lib.extreg_get_provider_by_id(999))
  end)

  itp('nonexistent provider name returns nil', function()
    eq(nil, lib.extreg_get_provider('nonexistent'))
  end)

  itp('provider count is tracked correctly', function()
    eq(2, lib.extreg_provider_count)
  end)
end)
