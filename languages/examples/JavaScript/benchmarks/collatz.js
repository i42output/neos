function steps(n) {
    let s = 0;
    while (n !== 1) {
        n = n % 2 === 0 ? n / 2 : 3 * n + 1;
        s++;
    }
    return s;
}
let best = 0, arg = 0;
for (let i = 1; i < 1000000; i++) {
    const k = steps(i);
    if (k > best) { best = k; arg = i; }
}
console.log(arg, best);
