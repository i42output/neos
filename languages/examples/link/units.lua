-- Linking example (see units.sh): the first Lua translation unit

function half(x)
    return x / 2
end

print("units.lua: half(9) = " .. half(9))
