local function f(l, m)
  local cnt = 0
  for i = 0, l - 1 do
    for j = 0, m - 1 do cnt = cnt + 1 end
  end
  return cnt
end

print(f(99999 + 1, 9999 + 1))
