local total, sign = 0.0, 1.0
for k = 0, 30000000 - 1 do
  total = total + sign / (2 * k + 1)
  sign = -sign
end
print(4 * total)
