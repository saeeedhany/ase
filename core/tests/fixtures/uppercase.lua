-- Test fixture for ase_plugin_host_tests (core/tests/test_plugin_host.c).
-- A real (if trivial) plugin: uppercases the whole buffer.
ase.register_command("lua_uppercase", function(buf)
    local len = ase.buffer_length(buf)
    local text = ase.buffer_get_text(buf, 0, len)
    ase.buffer_delete(buf, 0, len)
    ase.buffer_insert(buf, 0, string.upper(text))
end)
