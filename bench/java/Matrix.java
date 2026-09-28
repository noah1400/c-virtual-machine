// Multiplies two integer matrices: nested loops, indexing and multiplication
class Matrix {
    static final int N = 240;

    public static void main(String[] args) {
        int[] a = new int[N * N];
        int[] b = new int[N * N];
        int[] c = new int[N * N];
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
        System.out.println(total);
    }
}
