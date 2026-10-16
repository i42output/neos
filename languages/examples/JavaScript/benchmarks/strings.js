let total = 0;
for (let i = 0; i < 300000; i++) {
    const t = "item " + i;
    total += t.length;
}
console.log(total);
