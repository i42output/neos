#include <stdio.h>

static int mandelbrot(int size, int limit)
{
    int inside = 0;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            double const cr = 2.0 * x / size - 1.5;
            double const ci = 2.0 * y / size - 1.0;
            double zr = 0.0, zi = 0.0;
            int i = 0;
            while (i < limit && zr * zr + zi * zi <= 4.0)
            {
                double const t = zr * zr - zi * zi + cr;
                zi = 2.0 * zr * zi + ci;
                zr = t;
                ++i;
            }
            if (i == limit)
                ++inside;
        }
    return inside;
}

int main(void)
{
    printf("%d\n", mandelbrot(600, 200));
    return 0;
}
