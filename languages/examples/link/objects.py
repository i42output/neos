# Linking example (see objects.sh): Python holding a JavaScript object (objects.js) and giving it back to JavaScript, and
# JavaScript holding a Python object and giving it back

print("origin is", origin)
print("length(origin) =", length(origin))

shopping = ["eggs", "milk"]
keep(shopping)
kept().append("bread")
print("the list JavaScript kept is the list:", kept() is shopping, shopping)
