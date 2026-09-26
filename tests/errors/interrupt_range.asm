; expect-error: interrupt vector 300 is out of range (0 to 255)
.text
    INT #300
