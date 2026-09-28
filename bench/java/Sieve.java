// Primes below a limit with a sieve, and the sum of their digits: array writes and division
class Sieve {
    static int digits(int n) {
        int sum = 0;
        while (n > 0) {
            sum += n % 10;
            n /= 10;
        }
        return sum;
    }

    public static void main(String[] args) {
        int limit = 12000000;
        boolean[] composite = new boolean[limit];
        int count = 0;
        int total = 0;
        for (int n = 2; n < limit; n += 1) {
            if (!composite[n]) {
                count += 1;
                total += digits(n);
                for (int m = n * 2; m < limit; m += n) {
                    composite[m] = true;
                }
            }
        }
        System.out.println(count + " " + total);
    }
}
