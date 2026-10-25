-- Coroutines: generators, a producer and consumer, and coroutine.wrap

local function range(n)
  return coroutine.wrap(function()
    for i = 1, n do coroutine.yield(i) end
  end)
end

for i in range(5) do io.write(i, " ") end
print()

local producer = coroutine.create(function()
  for _, item in ipairs({ "apple", "banana", "cherry" }) do
    coroutine.yield(item)
  end
  return "done"
end)

while true do
  local ok, item = coroutine.resume(producer)
  print(ok, item, coroutine.status(producer))
  if coroutine.status(producer) == "dead" then break end
end

-- (values passed in both directions)
local echo = coroutine.create(function(a, b)
  local c = coroutine.yield(a + b)
  local d, e = coroutine.yield(c * 2)
  return d + e
end)
print(coroutine.resume(echo, 1, 2))
print(coroutine.resume(echo, 10))
print(coroutine.resume(echo, 3, 4))
print(coroutine.resume(echo))
