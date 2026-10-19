// Types are inferred: variables from their values, parameters from arguments and functions from what they return

// types are inferred: parameters from arguments, results from return statements
const LIMIT = 30;                       // a global used by functions declared before and after it
let calls = 0;

console.log(describe(classify(7)), describe(classify(-2)));
console.log(repeat("ab", 3), greet("world"), isEven(10), isEven(7));

function classify(n) {
    calls++;
    if (n > 0) return "positive";
    else if (n < 0) return "negative";
    return "zero";
}

function describe(kind) {
    return `the number is ${kind}`;
}

function repeat(s, times) {
    let result = "";
    for (let i = 0; i < times; i++) result += s;
    return result;
}

function greet(who) { return "hello " + who; }
function isEven(n) { return n % 2 === 0; }

/* collatz with a global limit */
function longest() {
    let best = 0, arg = 0;
    for (let i = 1; i < LIMIT; i++) {
        let n = i, steps = 0;
        while (n != 1) {
            n = (n % 2 == 0) ? n / 2 : 3 * n + 1;
            ++steps;
        }
        if (steps > best) { best = steps; arg = i; }
    }
    return arg * 1000 + best;
}
console.log(calls)
console.log(longest());

let x = 1;
{
    let x = 2;
    console.log("inner", x);
}
console.log("outer", x);
for (var i = 0; i < 3; i++) {}
console.log("var i after loop", i);
function shadow(x) {
    let y = x * 10;
    if (y > 5) {
        let y = 99;
        console.log("block y", y);
    }
    return y;
}
console.log(shadow(1));
let counter = 0;
function bump() { counter += 1; return counter; }
bump(); bump();
console.log("counter", counter, bump());
let s = "a";
s += "b";
s = s + 1;
console.log(s);
let flag = false;
flag = !flag;
console.log(flag);
let u;
if (x > 0) u = "set"; else u = "unset";
console.log(u);
