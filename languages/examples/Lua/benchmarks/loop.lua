local total = 0
for i = 0, 30000000 - 1 do
  total = total + (i * i) % 7
end
print(total)
