import java.nio.charset.StandardCharsets;

// Scans generated text for words and numbers as a lexer does: byte loads and branches
class Text {
    static int state = 0x92D68CA2;

    static int next() {
        state ^= state << 13;
        state ^= state >>> 17;
        state ^= state << 5;
        return state;
    }

    public static void main(String[] args) {
        byte[] alphabet = "etaoinshrdlucmfwypvbg    0123456".getBytes(StandardCharsets.US_ASCII);
        int n = 6000000;
        byte[] text = new byte[n];
        for (int i = 0; i < n; i += 1) {
            text[i] = alphabet[(next() >>> 11) & 31];
        }
        int words = 0;
        int numbers = 0;
        int letters = 0;
        int longest = 0;
        int sum = 0;
        int i = 0;
        while (i < n) {
            byte c = text[i];
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
                sum = sum * 31 + length;
            } else if (c >= '0' && c <= '9') {
                int value = 0;
                while (i < n && text[i] >= '0' && text[i] <= '9') {
                    value = (value * 10 + (text[i] - '0')) & 16777215;
                    i += 1;
                }
                numbers += 1;
                sum ^= value;
            } else {
                i += 1;
            }
        }
        System.out.println(words + " " + numbers + " " + letters + " " + longest + " " + Integer.toUnsignedString(sum));
    }
}
