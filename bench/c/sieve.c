#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

// Primes below a limit with a sieve, and the sum of their digits: array writes and division
static int digits(int n) {
    int sum = 0;
    while (n > 0) {
        sum += n % 10;
        n /= 10;
    }
    return sum;
}

int main(void) {
    int limit = 12000000;
    bool *composite = calloc(limit, sizeof(bool));
    int count = 0;
    int total = 0;
    for (int n = 2; n < limit; n += 1) {
        if (!composite[n]) {
            count += 1;
            total += digits(n);
            for (int m = n * 2; m < limit; m += n) {
                composite[m] = true;
            }
        }
    }
    printf("%d %d\n", count, total);
    free(composite);
    return 0;
}
