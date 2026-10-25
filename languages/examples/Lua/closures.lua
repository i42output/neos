-- Closures and upvalues: counters, generators and varargs

local function counter(start, step)
  local count = start or 0
  return function()
    count = count + (step or 1)
    return count
  end
end

local c1, c2 = counter(), counter(100, 10)
print(c1(), c1(), c2(), c1(), c2())

-- (each iteration has its own loop variable)
local functions = {}
for i = 1, 3 do functions[i] = function() return i * i end end
print(functions[1](), functions[2](), functions[3]())

local function sum(...)
  local total = 0
  for _, v in ipairs({ ... }) do total = total + v end
  return total, select("#", ...)
end
print(sum(1, 2, 3, 4.5))

local function compose(f, g) return function(...) return f(g(...)) end end
local inc_then_double = compose(function(x) return x * 2 end, function(x) return x + 1 end)
print(inc_then_double(20))
