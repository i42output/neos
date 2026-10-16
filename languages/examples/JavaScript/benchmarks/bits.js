// integer hashing with 32-bit operations
function hash(n) {
    let h = 2166136261 | 0;
    for (let i = 0; i < n; i++) {
        h ^= i & 0xFF;
        h = Math.imul(h, 16777619);
        h = (h << 5) ^ (h >>> 27) ^ i;
    }
    return h >>> 0;
}
console.log(hash(20000000));
