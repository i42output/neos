function mandelbrot(size, limit) {
    let inside = 0;
    for (let y = 0; y < size; y++) {
        for (let x = 0; x < size; x++) {
            const cr = 2 * x / size - 1.5, ci = 2 * y / size - 1;
            let zr = 0, zi = 0, i = 0;
            while (i < limit && zr * zr + zi * zi <= 4) {
                const t = zr * zr - zi * zi + cr;
                zi = 2 * zr * zi + ci;
                zr = t;
                i++;
            }
            if (i === limit) inside++;
        }
    }
    return inside;
}
console.log(mandelbrot(600, 200));
