-- A tiny assert-based test runner -- no LuaRocks/busted install needed.

local M = { failures = 0, count = 0 }

function M.test(name, fn)
  M.count = M.count + 1
  local ok, err = pcall(fn)
  if ok then
    print("  ok   - " .. name)
  else
    M.failures = M.failures + 1
    print("  FAIL - " .. name .. ": " .. tostring(err))
  end
end

function M.eq(got, want, msg)
  if got ~= want then
    error(string.format("%s: expected %s, got %s", msg or "mismatch", tostring(want), tostring(got)), 2)
  end
end

function M.summary()
  print(string.format("%d/%d passed", M.count - M.failures, M.count))
  return M.failures
end

return M
