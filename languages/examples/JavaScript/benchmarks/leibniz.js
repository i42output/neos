let total = 0, sign = 1;
for (let k = 0; k < 30000000; k++) {
    total += sign / (2 * k + 1);
    sign = -sign;
}
console.log(4 * total);
