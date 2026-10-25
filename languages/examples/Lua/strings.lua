-- Strings: patterns, captures, gsub and string.format

local text = "The quick brown fox jumps over the lazy dog"

local words = {}
for word in text:gmatch("%a+") do words[#words + 1] = word:lower() end
table.sort(words)
print(table.concat(words, ","))

print(text:find("brown"), text:match("(%a+) fox"))
print((text:gsub("(%a+)", function(w) return #w > 4 and w:upper() or nil end)))

local date = "2026-10-25"
local year, month, day = date:match("(%d+)-(%d+)-(%d+)")
print(string.format("%02d/%02d/%04d", tonumber(day), tonumber(month), tonumber(year)))

print(string.format("%5.2f|%-8s|%x|%q", math.pi, "left", 255, "a \"quoted\"\nline"))
print(("abc"):rep(3, "-"), ("hello"):reverse(), ("%d items"):format(42))
print(utf8.char(72, 233, 8364, 128512), utf8.len("héllo"))
