// Linking example (see callbacks.sh): JavaScript functions that take functions (callbacks), which Python (callbacks.py) calls with
// its own functions and lambdas

function twice(f, x) {
    return f(f(x));
}

function compose(f, g) {
    return x => f(g(x));
}

function each(count, f) {
    for (let i = 1; i <= count; ++i)
        f(i);
}
