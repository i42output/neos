#include <stdio.h>

int main(void)
{
    double total = 0.0, sign = 1.0;
    for (int k = 0; k < 30000000; ++k)
    {
        total += sign / (2.0 * k + 1.0);
        sign = -sign;
    }
    printf("%.15g\n", 4.0 * total);
    return 0;
}
