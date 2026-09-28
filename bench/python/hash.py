# Counts keys in a chained hash table: allocation, hashing and pointer chasing
BUCKETS = 65521


class Entry:
    __slots__ = ("key", "count", "next")

    def __init__(self, key, next):
        self.key = key
        self.count = 0
        self.next = next


# FNV-1a over the four bytes of the key
def hash_key(key):
    h = 2166136261
    k = key
    for i in range(4):
        h ^= k & 255
        h = (h * 16777619) & 0xFFFFFFFF
        k >>= 8
    return h


def find(table, key):
    e = table[hash_key(key) % BUCKETS]
    while e is not None and e.key != key:
        e = e.next
    return e


def main():
    table = [None] * BUCKETS
    distinct = 0
    for i in range(4000000):
        key = i % 100003 * 7919 % 100003
        e = find(table, key)
        if e is None:
            bucket = hash_key(key) % BUCKETS
            e = Entry(key, table[bucket])
            table[bucket] = e
            distinct += 1
        e.count += 1
    print(distinct, find(table, 12345).count, find(table, 99999).count)


main()
