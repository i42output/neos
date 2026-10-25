-- Errors: error, pcall, xpcall, error objects and to-be-closed variables

local function divide(a, b)
  if b == 0 then error("division by zero", 2) end
  return a / b
end

print(pcall(divide, 10, 2))
print(pcall(divide, 1, 0))

local ok, err = pcall(error, { code = 42 })
print(ok, type(err), err.code)

print(xpcall(function() return nil + 1 end, function(message) return "handled: " .. message end))

local function resource(name)
  return setmetatable({ name = name }, {
    __close = function(r, e) print("closing " .. r.name, e) end
  })
end

do
  local a <close> = resource("a")
  local b <close> = resource("b")
  print("using " .. a.name .. " and " .. b.name)
end

print(pcall(function()
  local r <close> = resource("r")
  error("failed")
end))
