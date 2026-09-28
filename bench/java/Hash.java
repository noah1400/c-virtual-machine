// Counts keys in a chained hash table: allocation, hashing and pointer chasing
class Hash {
    static class Entry {
        int key;
        int count;
        Entry next;
    }

    static final int BUCKETS = 65521;

    // FNV-1a over the four bytes of the key
    static int hash(int key) {
        int h = 0x811C9DC5;
        int k = key;
        for (int i = 0; i < 4; i += 1) {
            h ^= k & 255;
            h *= 16777619;
            k >>>= 8;
        }
        return h;
    }

    static Entry find(Entry[] table, int key) {
        Entry e = table[Integer.remainderUnsigned(hash(key), BUCKETS)];
        while (e != null && e.key != key) {
            e = e.next;
        }
        return e;
    }

    public static void main(String[] args) {
        Entry[] table = new Entry[BUCKETS];
        int distinct = 0;
        for (int i = 0; i < 4000000; i += 1) {
            int key = i % 100003 * 7919 % 100003;
            Entry e = find(table, key);
            if (e == null) {
                int bucket = Integer.remainderUnsigned(hash(key), BUCKETS);
                e = new Entry();
                e.key = key;
                e.next = table[bucket];
                table[bucket] = e;
                distinct += 1;
            }
            e.count += 1;
        }
        System.out.println(distinct + " " + find(table, 12345).count + " " + find(table, 99999).count);
    }
}
