local function is_prime(n)
  if n < 2 then return false end
  local d = 2
  while d * d <= n do
    if n % d == 0 then return false end
    d = d + 1
  end
  return true
end
local count = 0
for n = 0, 2000000 - 1 do
  if is_prime(n) then count = count + 1 end
end
print(count)
