#include <stdio.h>
#include <stdlib.h>

// Multiplies two integer matrices: nested loops, indexing and multiplication
#define N 240

int main(void) {
    int *a = calloc(N * N, sizeof(int));
    int *b = calloc(N * N, sizeof(int));
    int *c = calloc(N * N, sizeof(int));
    for (int i = 0; i < N * N; i += 1) {
        a[i] = i % 17 - 8;
        b[i] = i % 13 - 6;
    }
    for (int round = 0; round < 2; round += 1) {
        for (int i = 0; i < N; i += 1) {
            for (int j = 0; j < N; j += 1) {
                int sum = 0;
                for (int k = 0; k < N; k += 1) {
                    sum += a[i * N + k] * b[k * N + j];
                }
                c[i * N + j] = sum + round;
            }
        }
    }
    int total = 0;
    for (int i = 0; i < N * N; i += 1) {
        total += c[i];
    }
    printf("%d\n", total);
    free(a);
    free(b);
    free(c);
    return 0;
}
