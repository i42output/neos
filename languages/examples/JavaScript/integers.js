// 32-bit integer operations (and variables that are only ever assigned 32-bit integers, which are i32)

let a = 0x7FFFFFFF, b = -1;
console.log(a | 0, a + 1 | 0, (a + 1) >>> 0, b >>> 0, b >>> 0 === -1, (b | 0) === -1, (b >>> 0) === (b >>> 0));
console.log((5 & 3) + (6 & 7), (1 << 30) + (1 << 30), ((1 << 30) + (1 << 30)) | 0, 2 ** 32 | 0, 2 ** 31 | 0, -(2 ** 31) | 0);
console.log(Math.imul(0x7FFFFFFF, 2), Math.clz32(0), Math.clz32(-1), ~~3.7, ~~-3.7, 3.7 | 0, -3.7 >> 0, NaN | 0, Infinity | 0);
console.log(1e20 | 0, -1e20 | 0, 2 ** 52 + 3 | 0, 2 ** 53 | 0, 4294967296.5 | 0, (2 ** 51 + 7) | 0, -(2 ** 51) - 9 | 0);
function hash(n) {
    let h = 2166136261 | 0;
    for (let i = 0; i < n; i++) {
        h ^= i & 0xFF;
        h = Math.imul(h, 16777619);
        h = (h << 5) ^ (h >>> 27) ^ i;
    }
    return h >>> 0;
}
console.log(hash(1000), (hash(10) & 1) === 0, hash(3) % 7, typeof (1 << 2));
let x = 5;
x <<= 2; x |= 1; x ^= 3; x &= 0xF; x >>= 1; x >>>= 0;
console.log(x);

// variables only assigned 32-bit integers are i32: arithmetic on them is still double arithmetic
let p = 0x7FFFFFFF | 0, q = 1 << 30;
let c = p + p, d = q * 4, e = p - -p;
console.log(p, q, c, d, e, p / 2, -p, p % 1000, p ** 2);
let h = 5;
h = h ^ 3;
h <<= 28;
console.log(h, h + h, h * h, h === -1073741824, h == -1073741824.0, h > 0, `${h}`, String(h), h.toString(16));
let m = 1;
for (let i = 0; i < 40; i++) m = Math.imul(m, 3) ^ i;
console.log(m, m >>> 0, (m >>> 0) === m, -m);
let n = 7;
n = n & 6;
n = n | 0;
n = 2.5;            // (assigned a non-integer: n is a double)
console.log(n);
function mix(x) {
    let r = x | 0;
    r = (r << 13) ^ r;
    r = Math.imul(r, 0x5bd1e995);
    return r ^ (r >>> 15);
}
let total = 0;
for (let i = 0; i < 1000; i++) total += mix(i) & 0xFFFF;
console.log(total, mix(12345), mix(-1));
let flags = 0;
flags |= 1 << 31;
console.log(flags, flags >>> 0, flags < 0);
let k = 0;
k = k ? 1 : 2;
console.log(k);
