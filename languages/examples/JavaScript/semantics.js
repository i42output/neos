// JavaScript semantics: values are converted as JavaScript converts them (the output must be identical to Node's)

const name = "neos";
let greeting = "Hello, " + name + "!";
console.log(greeting);
console.log("1" + 2, 1 + "2", 1 + 2 + "3", "3" * "4", true + 1, "5" - 2);
console.log(7 / 2, 7 % 3, -7 % 3, 7 % -3, 5.5 % 2, 2 ** 10, 2 ** -1);
console.log(1 / 0, -1 / 0, 0 / 0, -0 + 0);
console.log(typeof 1, typeof "s", typeof true);
console.log(10 == "10", 10 === 10, "a" < "b", "B" < "a", 2 < "10", "2" < "10");
console.log(5 & 3, 5 | 3, 5 ^ 3, ~5, 1 << 31, -16 >> 2, -16 >>> 28);
console.log(Math.floor(-2.5), Math.ceil(2.1), Math.round(2.5), Math.round(-2.5), Math.round(-0.4), Math.abs(-3), Math.max(1, 5, 3), Math.min(4, 2), Math.sqrt(2));
console.log(Number.MAX_SAFE_INTEGER, Number.EPSILON, Math.PI);
let s = "JavaScript";
console.log(s.length, s.charAt(4), s[0], s.toUpperCase(), s.toLowerCase(), s.indexOf("Script"), s.slice(-6), s.substring(4, 1));
console.log("  pad ".trim() + "|", "7".padStart(3, "0"), "ab".repeat(3), s.includes("va"), s.startsWith("Java"), s.charCodeAt(0));
console.log((3.14159).toFixed(2), (255).toString(16), (0.5).toFixed(0), (1.005).toFixed(2), (-1.5).toFixed(0));
console.log(parseInt("42px"), parseFloat("3.5e2xyz"), Number("  12  "), Number("1x"), parseInt("ff", 16), String(12.50));
console.log(isNaN(NaN), isFinite(1/0), Number.isInteger(5.0), Number.isInteger(5.5));
let flag = !(3 > 4) && 2 > 1;
console.log(flag, !"", !!"x", !0, 0 || 5, 3 && 4, "" || "default");
let x;
x = 42;
console.log(x);
let big = 123456789012345680000;
console.log(big, 1e-7, 0.000001, 1.5e300 * 1e10, 2 ** 53 + 1, 0.1 * 3);
console.log(String.fromCharCode(72, 105), "é".length, "😀".length);

// comments, semicolon-free code, nested conditionals
let a = 5
let b = 7 /* inline */ , c = a * b
console.log(c)
function grade(score) {
  return score >= 90 ? "A" : score >= 80 ? "B" : score >= 70 ? "C" : "F"
}
console.log(grade(95), grade(85), grade(72), grade(10))
let i = 0
while (true) {
  i++
  if (i >= 5) break
}
console.log(i)
let n = 0
for (;;) { if (++n === 3) break; }
console.log(n)
console.log(0.1 + 0.2 === 0.3, Math.abs(0.1 + 0.2 - 0.3) < Number.EPSILON)
console.log(1e21 + 1, 123e-20, -1e-7, 100 / 3, (100 / 3).toFixed(4))
console.log(Number.parseFloat("1.5"), Number.isSafeInteger(2 ** 53), 2 ** 31 | 0, (2 ** 32 + 5) >>> 0, -1 >>> 0)
console.log("abc".at(-1), "abc"[1], "a-b-c".lastIndexOf("-"), "Hello".endsWith("lo"), "x".concat("y", 1, true))
console.log(typeof a === "number" ? "num" : "other")
console.log(Math.sign(-3), Math.sign(0), Math.trunc(-4.7), Math.hypot(3, 4), Math.cbrt(27), Math.log2(8), Math.imul(3, 4), Math.clz32(1))
