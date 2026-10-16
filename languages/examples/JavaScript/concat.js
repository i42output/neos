// string + integer (fused) and its neighbours
for (let i = -3; i < 3; i++) console.log("v" + i, `t${i}`, i + "x", "a" + i + "b", ("" + i).length);
const big = 2147483647, small = -2147483648;
console.log("max " + big, "min " + small, "neg " + -5, "f " + 2.5, "z " + -0, "e " + 1e21);
let s = "";
for (let i = 0; i < 12; i++) s = s + i;
console.log(s, s.length, "é" + 7, ("é" + 7).length, ("😀" + 10).length);
