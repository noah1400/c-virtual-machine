# Scans generated text for words and numbers as a lexer does: byte loads and branches
state = 2463534242


def next_number():
    global state
    state ^= (state << 13) & 0xFFFFFFFF
    state ^= state >> 17
    state ^= (state << 5) & 0xFFFFFFFF
    return state


def main():
    alphabet = b"etaoinshrdlucmfwypvbg    0123456"
    n = 6000000
    text = bytearray(n)
    for i in range(n):
        text[i] = alphabet[(next_number() >> 11) & 31]
    words = 0
    numbers = 0
    letters = 0
    longest = 0
    total = 0
    i = 0
    while i < n:
        c = text[i]
        if 97 <= c <= 122:
            start = i
            while i < n and 97 <= text[i] <= 122:
                i += 1
            length = i - start
            words += 1
            letters += length
            if length > longest:
                longest = length
            total = (total * 31 + length) & 0xFFFFFFFF
        elif 48 <= c <= 57:
            value = 0
            while i < n and 48 <= text[i] <= 57:
                value = (value * 10 + text[i] - 48) & 16777215
                i += 1
            numbers += 1
            total ^= value
        else:
            i += 1
    print(words, numbers, letters, longest, total)


main()
