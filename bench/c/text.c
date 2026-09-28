#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Scans generated text for words and numbers as a lexer does: byte loads and branches
static uint32_t state = 2463534242u;

static uint32_t next(void) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

int main(void) {
    const char *alphabet = "etaoinshrdlucmfwypvbg    0123456";
    int n = 6000000;
    unsigned char *text = calloc(n, 1);
    for (int i = 0; i < n; i += 1) {
        text[i] = (unsigned char)alphabet[(next() >> 11) & 31];
    }
    int words = 0;
    int numbers = 0;
    int letters = 0;
    int longest = 0;
    uint32_t sum = 0;
    int i = 0;
    while (i < n) {
        unsigned char c = text[i];
        if (c >= 'a' && c <= 'z') {
            int start = i;
            while (i < n && text[i] >= 'a' && text[i] <= 'z') {
                i += 1;
            }
            int length = i - start;
            words += 1;
            letters += length;
            if (length > longest) {
                longest = length;
            }
            sum = sum * 31 + (uint32_t)length;
        } else if (c >= '0' && c <= '9') {
            int value = 0;
            while (i < n && text[i] >= '0' && text[i] <= '9') {
                value = (value * 10 + (text[i] - '0')) & 16777215;
                i += 1;
            }
            numbers += 1;
            sum ^= (uint32_t)value;
        } else {
            i += 1;
        }
    }
    printf("%d %d %d %d %u\n", words, numbers, letters, longest, sum);
    free(text);
    return 0;
}
