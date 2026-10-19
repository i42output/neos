function f(l, m) {
    let cnt = 0;
    for (let i = 0; i < l; i++)
        for (let j = 0; j < m; j++) cnt += 1;
    return cnt;
}

console.log(f(99_999 + 1, 9_999 + 1));
