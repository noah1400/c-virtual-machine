#include <stdio.h>

// Recursive Fibonacci: calls and returns
static int fib(int n) {
    if (n < 2) {
        return n;
    }
    return fib(n - 1) + fib(n - 2);
}

int main(void) {
    printf("%d\n", fib(36));
    return 0;
}
