; expect-error: size 5000 is out of range (0 to 4095)
.text
    MEMCPY R1, R2, #5000
