-- Fibonacci numbers: recursive, iterative and memoized (a table with an __index metamethod)

local function fib(n)
  if n < 2 then return n end
  return fib(n - 1) + fib(n - 2)
end

local function fib_iterative(n)
  local a, b = 0, 1
  for _ = 1, n do a, b = b, a + b end
  return a
end

local memo = setmetatable({ [0] = 0, [1] = 1 }, {
  __index = function(t, n)
    local value = t[n - 1] + t[n - 2]
    t[n] = value
    return value
  end
})

for n = 0, 20 do
  io.write(fib(n), n < 20 and " " or "\n")
end
print(fib_iterative(90), memo[90])
