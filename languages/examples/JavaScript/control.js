// Control flow: loops, break/continue, switch

let total = 0;
for (let i = 0; i < 100; i++) {
    if (i % 3 === 0) continue;
    if (i > 50) break;
    total += i;
}
console.log("total", total);
let n = 27, steps = 0;
while (n !== 1) {
    n = n % 2 === 0 ? n / 2 : 3 * n + 1;
    steps++;
}
console.log(steps);
let k = 0;
do {
    k += 2;
} while (k < 7);
console.log(k);
outer();
function outer() {
    let count = 0;
    for (let a = 0; a < 5; a++)
        for (let b = 0; b < 5; b++) {
            if (b > a) break;
            count++;
        }
    console.log("pairs", count);
}

function name(n) {
    switch (n) {
        case 0: return "zero";
        case 1:
        case 2: return "small";
        default: return "other";
        case 100: return "hundred";
    }
}
for (let i = 0; i < 4; i++) console.log(i, name(i));
console.log(name(100), name(-1));
let log = "";
for (let i = 0; i < 6; i++) {
    switch (i % 3) {
        case 0:
            log += "a";
        case 1:
            log += "b";
            break;
        default:
            if (i > 3) continue;
            log += "c";
    }
    log += "|";
}
console.log(log);
function kind(s) {
    switch (s) {
        case "x": case "y": return 1;
        case "z": return 2;
    }
    return 0;
}
console.log(kind("x"), kind("z"), kind("q"));
let t0 = performance.now(), t1 = Date.now();
console.log(typeof t0, t1 > 1.6e12);
let v = 3;
switch (v) {}
switch (v) { default: console.log("only default"); }
