# Primes below a limit with a sieve, and the sum of their digits: array writes and division
def digits(n):
    total = 0
    while n > 0:
        total += n % 10
        n //= 10
    return total


def main():
    limit = 12000000
    composite = bytearray(limit)
    count = 0
    total = 0
    for n in range(2, limit):
        if not composite[n]:
            count += 1
            total += digits(n)
            for m in range(n * 2, limit, n):
                composite[m] = 1
    print(count, total)


main()
