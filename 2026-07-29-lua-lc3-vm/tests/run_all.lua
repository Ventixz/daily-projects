local script_dir = (arg[0]:match("(.*/)") or "./")

local failures = 0
failures = failures + dofile(script_dir .. "test_vm.lua")
print("")
failures = failures + dofile(script_dir .. "test_asm.lua")

os.exit(failures > 0 and 1 or 0)
