// Linking example (see three.sh): JavaScript calling Lua (three.lua) and Python (three.py), whose functions call Lua's

console.log(banner("Lua, Python and JavaScript, linked"));
console.log("fib(30) from Lua:", fib(30));
console.log("the first 15 Fibonacci numbers, Python calling Lua:", fib_table(15));
console.log("Lua's version:", lua_version());
