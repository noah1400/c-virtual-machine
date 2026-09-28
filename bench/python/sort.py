# Sorts pseudo-random numbers with quicksort and checks the order: array accesses and recursion
state = 12345


def next_number():
    global state
    state ^= (state << 13) & 0xFFFFFFFF
    state ^= state >> 17
    state ^= (state << 5) & 0xFFFFFFFF
    return state >> 1


def sort(a, low, high):
    while low < high:
        pivot = a[(low + high) // 2]
        i = low
        j = high
        while i <= j:
            while a[i] < pivot:
                i += 1
            while a[j] > pivot:
                j -= 1
            if i <= j:
                a[i], a[j] = a[j], a[i]
                i += 1
                j -= 1
        if j - low < high - i:
            sort(a, low, j)
            low = i
        else:
            sort(a, i, high)
            high = j


def main():
    n = 1000000
    a = [0] * n
    for i in range(n):
        a[i] = next_number() % 1000000
    sort(a, 0, n - 1)
    ok = True
    for i in range(1, n):
        if a[i - 1] > a[i]:
            ok = False
    print("true" if ok else "false", a[0], a[n // 2], a[n - 1])


main()
