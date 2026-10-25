local function steps(n)
  local s = 0
  while n ~= 1 do
    if n % 2 == 0 then n = n // 2 else n = 3 * n + 1 end
    s = s + 1
  end
  return s
end
local best, arg = 0, 0
for i = 1, 1000000 - 1 do
  local k = steps(i)
  if k > best then best, arg = k, i end
end
print(arg, best)
