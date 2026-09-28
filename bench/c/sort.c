#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

// Sorts pseudo-random numbers with quicksort and checks the order: array accesses and recursion
static uint32_t state = 12345;

static int next(void) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return (int)(state >> 1);
}

static void sort(int *a, int low, int high) {
    while (low < high) {
        int pivot = a[(low + high) / 2];
        int i = low;
        int j = high;
        while (i <= j) {
            while (a[i] < pivot) {
                i += 1;
            }
            while (a[j] > pivot) {
                j -= 1;
            }
            if (i <= j) {
                int t = a[i];
                a[i] = a[j];
                a[j] = t;
                i += 1;
                j -= 1;
            }
        }
        if (j - low < high - i) {
            sort(a, low, j);
            low = i;
        } else {
            sort(a, i, high);
            high = j;
        }
    }
}

int main(void) {
    int n = 1000000;
    int *a = calloc(n, sizeof(int));
    for (int i = 0; i < n; i += 1) {
        a[i] = next() % 1000000;
    }
    sort(a, 0, n - 1);
    bool ok = true;
    for (int i = 1; i < n; i += 1) {
        if (a[i - 1] > a[i]) {
            ok = false;
        }
    }
    printf("%s %d %d %d\n", ok ? "true" : "false", a[0], a[n / 2], a[n - 1]);
    free(a);
    return 0;
}
