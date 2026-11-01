-- Linking example (see units.sh): a second Lua translation unit: it uses units.lua's function (Lua's units share their global table, as
-- chunks do) and the Python units' functions

print("units2.lua: half(cube(2)) = " .. half(cube(2)) .. ", square(6) = " .. square(6))
