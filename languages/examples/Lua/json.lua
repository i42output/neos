-- a small JSON encoder/decoder
local json = {}
local escapes = { ['"'] = '\\"', ['\\'] = '\\\\', ['\b'] = '\\b', ['\f'] = '\\f', ['\n'] = '\\n', ['\r'] = '\\r', ['\t'] = '\\t' }
local function encode_string(s)
  return '"' .. s:gsub('[%c"\\]', function(c) return escapes[c] or string.format("\\u%04x", c:byte()) end) .. '"'
end
local function is_array(t)
  local n = 0
  for k in pairs(t) do
    if type(k) ~= "number" or k <= 0 or math.floor(k) ~= k then return false end
    n = math.max(n, k)
  end
  return n == #t
end
function json.encode(v)
  local t = type(v)
  if t == "nil" then return "null"
  elseif t == "boolean" then return tostring(v)
  elseif t == "number" then
    if v ~= v or v == math.huge or v == -math.huge then error("cannot encode " .. tostring(v)) end
    if math.type(v) == "integer" then return tostring(v) end
    return string.format("%.14g", v)
  elseif t == "string" then return encode_string(v)
  elseif t == "table" then
    local parts = {}
    if is_array(v) then
      for i = 1, #v do parts[i] = json.encode(v[i]) end
      return "[" .. table.concat(parts, ",") .. "]"
    end
    local keys = {}
    for k in pairs(v) do keys[#keys + 1] = tostring(k) end
    table.sort(keys)
    for _, k in ipairs(keys) do parts[#parts + 1] = encode_string(k) .. ":" .. json.encode(v[k]) end
    return "{" .. table.concat(parts, ",") .. "}"
  end
  error("cannot encode type " .. t)
end
function json.decode(s)
  local pos = 1
  local function ws() pos = s:find("%S", pos) or #s + 1 end
  local value
  local function str()
    local out = {}
    pos = pos + 1
    while true do
      local c = s:sub(pos, pos)
      if c == '"' then pos = pos + 1 break end
      if c == "\\" then
        local e = s:sub(pos + 1, pos + 1)
        local map = { b = "\b", f = "\f", n = "\n", r = "\r", t = "\t" }
        if e == "u" then out[#out + 1] = utf8.char(tonumber(s:sub(pos + 2, pos + 5), 16)) pos = pos + 6
        else out[#out + 1] = map[e] or e pos = pos + 2 end
      else out[#out + 1] = c pos = pos + 1 end
    end
    return table.concat(out)
  end
  function value()
    ws()
    local c = s:sub(pos, pos)
    if c == "{" then
      local obj = {} pos = pos + 1 ws()
      if s:sub(pos, pos) == "}" then pos = pos + 1 return obj end
      repeat
        ws() local k = str() ws() pos = pos + 1 obj[k] = value() ws()
        local d = s:sub(pos, pos) pos = pos + 1
      until d == "}"
      return obj
    elseif c == "[" then
      local arr = {} pos = pos + 1 ws()
      if s:sub(pos, pos) == "]" then pos = pos + 1 return arr end
      repeat arr[#arr + 1] = value() ws() local d = s:sub(pos, pos) pos = pos + 1 until d == "]"
      return arr
    elseif c == '"' then return str()
    elseif s:find("^true", pos) then pos = pos + 4 return true
    elseif s:find("^false", pos) then pos = pos + 5 return false
    elseif s:find("^null", pos) then pos = pos + 4 return nil
    else
      local num = s:match("^-?%d+%.?%d*[eE]?[-+]?%d*", pos)
      pos = pos + #num
      return math.tointeger(tonumber(num)) or tonumber(num)
    end
  end
  return value()
end
local data = { name = "neos", version = 1.5, tags = {"lua", "wasm", "jit"}, nested = { a = 1, b = { true, false } }, esc = "line\nbreak \"quoted\" \1" }
local encoded = json.encode(data)
print(encoded)
local decoded = json.decode(encoded)
print(decoded.name, decoded.version, decoded.tags[2], decoded.nested.b[1], decoded.esc == data.esc)
print(json.encode(json.decode('[1, 2.5, "x", {"k": [null, true]}, -3e2]')))
print(pcall(json.encode, 0/0))
