; expect-error: size -1 is out of range (0 to 4294967295)
.text
    MEMCPY R1, R2, #-1
