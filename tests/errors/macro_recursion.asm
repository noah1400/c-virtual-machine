; expect-error: macro FOREVER is expanded too deeply
.macro FOREVER
    FOREVER
.endm
.text
    FOREVER
