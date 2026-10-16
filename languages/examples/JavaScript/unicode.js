// string lengths in UTF-16 code units (the string header's count) across literals, concatenation and runtime results
const a = "héllo", b = "日本語", c = "😀x", d = "";
console.log(a.length, b.length, c.length, d.length);
const e = a + b + c;
console.log(e.length, (e + e).length, `${b}${c}!`.length);
let s = "";
for (let i = 0; i < 5; i++) s = s + "é" + i;
console.log(s, s.length);
console.log(String(12345).length, (-7).toString().length, (2.5).toString().length, ("x".repeat(3) + "😀").length);
console.log(b.slice(1).length, c.toUpperCase().length, " ab ".trim().length, b.charAt(1).length);
let total = 0;
for (let i = 0; i < s.length; i++) total += s.charCodeAt(i);
console.log(total);
console.log(!!d, !!a, d ? 1 : 2);
for (let i = -3; i < 3; i++) console.log("v" + i, ("" + i).length);
