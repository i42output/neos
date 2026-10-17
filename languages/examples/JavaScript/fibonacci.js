// Fibonacci: recursion, template literals

function fib(n) {
    if (n < 2) return n;
    return fib(n - 1) + fib(n - 2);
}
console.log(fib(20));
for (let i = 0; i < 10; i++) {
    console.log(`fib(${i}) = ${fib(i)}`);
}
