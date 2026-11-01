-- Linking example (see three.sh): Lua functions that Python (three.py) and JavaScript (three.js) call

function fib(n)
    local a, b = 0, 1
    for _ = 1, n do
        a, b = b, a + b
    end
    return a
end

function lua_version()
    return _VERSION
end
