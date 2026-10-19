a = "héllo"
b = "日本語"
c = "😀x"
d = ""
print(len(a))
print(len(b))
print(len(c))
print(len(d))
e = a + b + c
print(len(e))
print(len(e + e))
s = ""
for i in range(5):
    s = s + "é" + str(i)
print(s)
print(len(s))
print(len(str(12345)))
print(len(str(-7)))
print(len(str(2.5)))
print(len("é"))
print(str(-9223372036854775807 - 1))
print(str(9223372036854775807))
print(str(0) + str(-0) + str(42))
for i in range(-3, 3):
    print("v" + str(i))
    print(len("é" + str(i)))
print("max " + str(9223372036854775807))
print("min " + str(-9223372036854775807 - 1))
s = ""
for i in range(12):
    s = s + str(i)
print(s)
print(len(s))
print(str(5) + "x")
print("😀" + str(10))
print(len("😀" + str(10)))
