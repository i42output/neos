local total = 0
for i = 0, 300000 - 1 do
  local t = "item " .. i
  total = total + #t
end
print(total)
