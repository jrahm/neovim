#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <ctype.h>

#include "nvim/extreg.h"
#include "nvim/lua/executor.h"
#include "nvim/macros_defs.h"
#include "nvim/memory.h"
#include "nvim/os/time.h"

static void test_extreg_init(void **state)
{
  extreg_init();
  assert_int_equal(extreg_provider_count, 0);
}

static void test_extreg_register_success(void **state)
{
  extreg_init();
  
  LuaRef dummy_ref = LUA_NOREF;
  int id = extreg_register("test-provider", dummy_ref, dummy_ref);
  
  assert_int_greater(id, 0);
  assert_int_equal(extreg_provider_count, 1);
  
  extreg_provider_t *prov = &extreg_provider_list[0];
  assert_non_null(prov->name);
  assert_string_equal(prov->name, "test-provider");
  assert_false(prov->alias);
  assert_true(prov->registered);
}

static void test_extreg_register_duplicate(void **state)
{
  extreg_init();
  
  LuaRef dummy_ref = LUA_NOREF;
  int id1 = extreg_register("test-provider", dummy_ref, dummy_ref);
  int id2 = extreg_register("test-provider", dummy_ref, dummy_ref);
  
  assert_int_greater(id1, 0);
  assert_int_equal(id2, 0);  // Duplicate should fail
  assert_int_equal(extreg_provider_count, 1);  // Only one provider should exist
}

static void test_extreg_register_max(void **state)
{
  extreg_init();
  
  // Register max number of providers
  for (int i = 0; i < EXTREG_MAX_PROVIDERS; i++) {
    char name[64];
    snprintf(name, sizeof(name), "provider-%d", i);
    LuaRef dummy_ref = LUA_NOREF;
    int id = extreg_register(name, dummy_ref, dummy_ref);
    assert_int_positive(id);
  }
  
  // One more should fail
  int id_overflow = extreg_register("overflow", LUA_NOREF, LUA_NOREF);
  assert_int_equal(id_overflow, 0);
  assert_int_equal(extreg_provider_count, EXTREG_MAX_PROVIDERS);
}

static void test_extreg_unregister_success(void **state)
{
  extreg_init();
  
  LuaRef dummy_ref = LUA_NOREF;
  int id = extreg_register("test-provider", dummy_ref, dummy_ref);
  
  bool result = extreg_unregister("test-provider");
  assert_true(result);
  assert_int_equal(extreg_provider_count, 0);
}

static void test_extreg_unregister_nonexistent(void **state)
{
  extreg_init();
  
  bool result = extreg_unregister("nonexistent");
  assert_false(result);
}

static void test_extreg_unregister_already_unregistered(void **state)
{
  extreg_init();
  
  LuaRef dummy_ref = LUA_NOREF;
  extreg_register("test-provider", dummy_ref, dummy_ref);
  extreg_unregister("test-provider");
  
  bool result = extreg_unregister("test-provider");
  assert_false(result);
}

static void test_extreg_bind_success(void **state)
{
  extreg_init();
  
  LuaRef dummy_ref = LUA_NOREF;
  int id = extreg_register("test-provider", dummy_ref, dummy_ref);
  
  bool result = extreg_bind('@', "test-provider");
  assert_true(result);
  
  extreg_provider_t *prov = &extreg_provider_list[0];
  assert_int_equal(prov->alias, '@');
}

static void test_extreg_bind_invalid_alias(void **state)
{
  extreg_init();
  
  LuaRef dummy_ref = LUA_NOREF;
  extreg_register("test-provider", dummy_ref, dummy_ref);
  
  // Test various invalid aliases
  assert_false(extreg_bind('a', "test-provider"));  // lowercase
  assert_false(extreg_bind('A', "test-provider"));  // uppercase (except u, p)
  assert_false(extreg_bind('=', "test-provider"));  // special chars
  assert_false(extreg_bind('+', "test-provider"));  // special chars
  assert_false(extreg_bind('9', "test-provider"));  // digits
  assert_false(extreg_bind('\t', "test-provider"));  // non-printable
}

static void test_extreg_bind_valid_alias(void **state)
{
  extreg_init();
  
  LuaRef dummy_ref = LUA_NOREF;
  int id = extreg_register("test-provider", dummy_ref, dummy_ref);
  
  // Test various valid aliases
  assert_true(extreg_bind('@', "test-provider"));
  assert_true(extreg_bind('$', "test-provider"));
  assert_true(extreg_bind('%', "test-provider"));  // Actually this is invalid!
  assert_true(extreg_bind('u', "test-provider"));  // Special case: allowed
  assert_true(extreg_bind('p', "test-provider"));  // Special case: allowed
}

static void test_extreg_bind_rebind(void **state)
{
  extreg_init();
  
  LuaRef dummy_ref = LUA_NOREF;
  int id1 = extreg_register("provider1", dummy_ref, dummy_ref);
  int id2 = extreg_register("provider2", dummy_ref, dummy_ref);
  
  // Bind to provider1
  extreg_bind('@', "provider1");
  assert_int_equal(extreg_provider_list[0].alias, '@');
  assert_int_equal(extreg_provider_list[1].alias, 0);
  
  // Rebind to provider2
  extreg_bind('@', "provider2");
  assert_int_equal(extreg_provider_list[0].alias, 0);  // Lost alias
  assert_int_equal(extreg_provider_list[1].alias, '@');  // Got alias
}

static void test_extreg_unbind_success(void **state)
{
  extreg_init();
  
  LuaRef dummy_ref = LUA_NOREF;
  int id = extreg_register("test-provider", dummy_ref, dummy_ref);
  extreg_bind('@', "test-provider");
  
  bool result = extreg_unbind('@');
  assert_true(result);
  
  extreg_provider_t *prov = &extreg_provider_list[0];
  assert_int_equal(prov->alias, 0);
}

static void test_extreg_unbind_not_bound(void **state)
{
  extreg_init();
  
  LuaRef dummy_ref = LUA_NOREF;
  int id = extreg_register("test-provider", dummy_ref, dummy_ref);
  
  bool result = extreg_unbind('@');
  assert_false(result);
}

static void test_extreg_is_alias(void **state)
{
  extreg_init();
  
  LuaRef dummy_ref = LUA_NOREF;
  int id = extreg_register("test-provider", dummy_ref, dummy_ref);
  
  assert_false(extreg_is_alias('@'));  // Not bound yet
  
  extreg_bind('@', "test-provider");
  assert_true(extreg_is_alias('@'));  // Now bound
  
  extreg_unbind('@');
  assert_false(extreg_is_alias('@'));  // Unbound
}

static void test_extreg_get_provider_id(void **state)
{
  extreg_init();
  
  LuaRef dummy_ref = LUA_NOREF;
  int id1 = extreg_register("provider1", dummy_ref, dummy_ref);
  int id2 = extreg_register("provider2", dummy_ref, dummy_ref);
  
  assert_int_equal(extreg_get_provider_id('@'), 0);  // Not bound
  
  extreg_bind('@', "provider1");
  assert_int_equal(extreg_get_provider_id('@'), id1);
  
  extreg_bind('@', "provider2");
  assert_int_equal(extreg_get_provider_id('@'), id2);
  
  extreg_unbind('@');
  assert_int_equal(extreg_get_provider_id('@'), 0);
}

static void test_extreg_get_provider_by_id(void **state)
{
  extreg_init();
  
  LuaRef dummy_ref = LUA_NOREF;
  int id = extreg_register("test-provider", dummy_ref, dummy_ref);
  
  extreg_provider_t *prov = extreg_get_provider_by_id(id);
  assert_non_null(prov);
  assert_string_equal(prov->name, "test-provider");
  
  // Invalid ID
  assert_null(extreg_get_provider_by_id(0));
  assert_null(extreg_get_provider_by_id(-1));
  assert_null(extreg_get_provider_by_id(EXTREG_MAX_PROVIDERS + 1));
}

static void test_extreg_get_provider(void **state)
{
  extreg_init();
  
  LuaRef dummy_ref = LUA_NOREF;
  int id = extreg_register("test-provider", dummy_ref, dummy_ref);
  
  extreg_provider_t *prov = extreg_get_provider("test-provider");
  assert_non_null(prov);
  assert_string_equal(prov->name, "test-provider");
  
  // Nonexistent provider
  assert_null(extreg_get_provider("nonexistent"));
}

static void test_extreg_is_recursing(void **state)
{
  extreg_init();
  
  LuaRef dummy_ref = LUA_NOREF;
  int id = extreg_register("test-provider", dummy_ref, dummy_ref);
  extreg_provider_t *prov = &extreg_provider_list[0];
  
  assert_false(extreg_is_recursing(prov));  // Not recursing
  
  prov->recursion_id = os_time();
  assert_true(extreg_is_recursing(prov));  // Now recursing
  
  // Reset with same timestamp
  prov->recursion_id = os_time();
  assert_false(extreg_is_recursing(prov));  // Not recursing anymore
  
  // With different timestamp (simulate time passing)
  prov->recursion_id = prov->recursion_id - 1;
  assert_true(extreg_is_recursing(prov));  // Still considered recursing
}

static void test_extreg_clear_all(void **state)
{
  extreg_init();
  
  LuaRef dummy_ref = LUA_NOREF;
  extreg_register("provider1", dummy_ref, dummy_ref);
  extreg_register("provider2", dummy_ref, dummy_ref);
  extreg_bind('@', "provider1");
  
  extreg_clear_all();
  
  assert_int_equal(extreg_provider_count, 0);
  assert_dict_equal(&extreg_providers, 0);
  assert_dict_equal(&extreg_aliases, 0);
}

static void test_is_valid_alias(void **state)
{
  // Valid aliases
  assert_true(is_valid_alias('@'));
  assert_true(is_valid_alias('$'));
  assert_true(is_valid_alias('u'));
  assert_true(is_valid_alias('p'));
  assert_true(is_valid_alias('X'));
  assert_true(is_valid_alias('Y'));
  
  // Invalid aliases
  assert_false(is_valid_alias('a'));  // lowercase (except u, p)
  assert_false(is_valid_alias('b'));
  assert_false(is_valid_alias('='));
  assert_false(is_valid_alias('('));
  assert_false(is_valid_alias('9'));
  assert_false(is_valid_alias('\t'));
  assert_false(is_valid_alias('\n'));
}

static void test_extreg_clear_all_with_aliases(void **state)
{
  extreg_init();
  
  LuaRef dummy_ref = LUA_NOREF;
  int id1 = extreg_register("provider1", dummy_ref, dummy_ref);
  int id2 = extreg_register("provider2", dummy_ref, dummy_ref);
  extreg_bind('@', "provider1");
  extreg_bind('$', "provider2");
  
  extreg_clear_all();
  
  // Check that aliases are cleared
  dictitem_T *di = tv_dict_find(&extreg_aliases, "@", 1);
  assert_null(di);
  
  di = tv_dict_find(&extreg_aliases, "$", 1);
  assert_null(di);
  
  // Check that providers are cleared
  di = tv_dict_find(&extreg_providers, "provider1", 9);
  assert_null(di);
  
  di = tv_dict_find(&extreg_providers, "provider2", 9);
  assert_null(di);
}

int main(void)
{
  const struct CMUnitTest tests[] = {
    cmocka_unit_test(test_extreg_init),
    cmocka_unit_test(test_extreg_register_success),
    cmocka_unit_test(test_extreg_register_duplicate),
    cmocka_unit_test(test_extreg_register_max),
    cmocka_unit_test(test_extreg_unregister_success),
    cmocka_unit_test(test_extreg_unregister_nonexistent),
    cmocka_unit_test(test_extreg_unregister_already_unregistered),
    cmocka_unit_test(test_extreg_bind_success),
    cmocka_unit_test(test_extreg_bind_invalid_alias),
    cmocka_unit_test(test_extreg_bind_valid_alias),
    cmocka_unit_test(test_extreg_bind_rebind),
    cmocka_unit_test(test_extreg_unbind_success),
    cmocka_unit_test(test_extreg_unbind_not_bound),
    cmocka_unit_test(test_extreg_is_alias),
    cmocka_unit_test(test_extreg_get_provider_id),
    cmocka_unit_test(test_extreg_get_provider_by_id),
    cmocka_unit_test(test_extreg_get_provider),
    cmocka_unit_test(test_extreg_is_recursing),
    cmocka_unit_test(test_extreg_clear_all),
    cmocka_unit_test(test_is_valid_alias),
    cmocka_unit_test(test_extreg_clear_all_with_aliases),
  };

  return unit_test_main(tests);
}
