# Multiplies two integer matrices: nested loops, indexing and multiplication
N = 240


def main():
    a = [0] * (N * N)
    b = [0] * (N * N)
    c = [0] * (N * N)
    for i in range(N * N):
        a[i] = i % 17 - 8
        b[i] = i % 13 - 6
    for round in range(2):
        for i in range(N):
            for j in range(N):
                total = 0
                for k in range(N):
                    total += a[i * N + k] * b[k * N + j]
                c[i * N + j] = total + round
    total = 0
    for i in range(N * N):
        total += c[i]
    print(total)


main()
