// integer range analysis: results must match doubles exactly (-0, overflow, counters)
function negzero(a, b) { return a * b; }
console.log(negzero(0, -5), negzero(-3, 0), negzero(4, 5));
let z = 0;
console.log(-z, 0 * -1, -0 + 0, z - z);
function rem(a, b) { return a % b; }
console.log(rem(-6, 3), rem(7, -3), rem(-7, 3), rem(6, 3), rem(5, 0));
// overflow past 2^31
let big = 2147483647;
big = big + 1;
console.log(big, big * 2, -2147483648 - 1);
let m = 65536;
console.log(m * m, m * m * m);
let acc = 1;
for (let i = 0; i < 40; i++) acc = acc * 3;
console.log(acc);
// counters
let s1 = 0;
for (let i = 0; i < 10; i++) s1 += i;
console.log(s1);
let s2 = 0;
for (let i = 10; i > 0; i--) s2 += i * i;
console.log(s2);
let s3 = 0;
for (let i = 0; i <= 2147483646; i += 1000000000) s3 += i;
console.log(s3);
let c = 0;
while (c < 2147483647) c += 1000000000;
console.log(c);
let d = 0;
while (d < 5) { d++; d++; }
console.log(d);
let e = 0;
for (let i = 0; i < 3; i++) { let k = 0; do { e++; k++; } while (k < 4); }
console.log(e);
let f = 2147483640;
while (f < 2147483647) { for (let j = 0; j < 3; j++) f++; }
console.log(f);
let g = 0;
for (let i = 0; i < 5; i++) { g = i; i++; }
console.log(g);
let h = 0;
for (let i = 0; i < 2147483647 && h < 3; i += 2147483646) h++;
console.log(h);
let w = 2147483600;
while (w < 2147483647) w += 7;
console.log(w);
function count(n) { let t = 0; for (let i = 0; i < n; i++) t += i % 3; return t; }
console.log(count(10), count(0), count(-5));
function half(n) { let r = 0; while (n > 1) { n = n / 2; r++; } return r; }
console.log(half(1024), half(1000));
function sq(n) { let r = 0; for (let d = 1; d * d <= n; d++) r = d; return r; }
console.log(sq(100), sq(99), sq(2147483647));
let x = 5;
console.log(-x, -(x - 5), (x - 5) * -1, x / 2, x % 2, -x % 2);
let u = 0;
for (let i = -5; i < 5; i++) u += i % 3;
console.log(u, String(-6 % 3));
let n1 = 7, n2 = 7.5;
console.log(n1 === 7, n1 < n2, n1 == 7, n1 !== 8);
