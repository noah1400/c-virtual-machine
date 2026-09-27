#!/bin/sh
# Usage: tests/run.sh [-u] [NAME...]
# Runs tests/programs/*.asm on the VM and checks that tests/errors/*.asm fail to assemble, or only the
# tests named. Expectations come from NAME.out, NAME.in and "; expect-exit:", "; expect-stderr:",
# "; expect-error:", "; asm-args:", "; vm-args:" and "; program-args:" lines; "; disk-sectors:" attaches
# an empty disk image and NAME.x holds debugger commands. -u rewrites NAME.out from the actual output of
# every program that exits as expected.

root=$(cd "$(dirname "$0")/.." && pwd)
vm="$root/vm"
asm="$root/vmasm"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT INT TERM
passed=0
failed=0
update=0
if [ "$1" = "-u" ]; then
    update=1
    shift
fi
names=" $* "

fail() {
    echo "FAIL $1: $2"
    failed=$((failed + 1))
}

wanted() {
    [ "$names" = "  " ] || case "$names" in *" $1 "*) true ;; *) false ;; esac
}

for name in $names; do
    [ -f "$root/tests/programs/$name.asm" ] || [ -f "$root/tests/errors/$name.asm" ] || fail "$name" "there is no such test"
done

expectation() {
    sed -n "s/^; $2: //p" "$1" | head -n 1
}

for src in "$root"/tests/programs/*.asm; do
    [ -e "$src" ] || continue
    name=$(basename "$src" .asm)
    base=${src%.asm}
    wanted "$name" || continue

    # Assemble with a relative path so debug info does not depend on the checkout location
    asm_args=$(expectation "$src" asm-args)
    if ! (cd "$root" && "$asm" $asm_args "tests/programs/$name.asm" -o "$tmp/$name.bin" 2> "$tmp/$name.log"); then
        fail "$name" "does not assemble: $(head -n 1 "$tmp/$name.log")"
        continue
    fi

    input=/dev/null
    [ -f "$base.in" ] && input="$base.in"
    # Programs run inside the scratch directory so the files they create do not leak; the
    # instruction limit turns a runaway program into a failure instead of a hang
    args=$(expectation "$src" vm-args)
    program_args=$(expectation "$src" program-args)
    if [ -f "$base.x" ]; then
        cp "$base.x" "$tmp/$name.x"
        args="$args -x $name.x"
    fi
    sectors=$(expectation "$src" disk-sectors)
    if [ -n "$sectors" ]; then
        dd if=/dev/zero of="$tmp/$name.img" bs=512 count="$sectors" 2> /dev/null
        args="$args -b $name.img"
    fi
    (cd "$tmp" && "$vm" -n 10000000 $args "$name.bin" $program_args < "$input" > "$name.out" 2> "$name.err")
    status=$?

    expected_status=$(expectation "$src" expect-exit)
    expected_stderr=$(expectation "$src" expect-stderr)
    expected_out="$base.out"
    if [ ! -f "$expected_out" ]; then
        expected_out="$tmp/empty"
        : > "$expected_out"
    fi

    if [ "$update" -eq 1 ] && [ "$status" = "${expected_status:-0}" ] && ! cmp -s "$expected_out" "$tmp/$name.out"; then
        if [ -s "$tmp/$name.out" ]; then
            cp "$tmp/$name.out" "$base.out"
        else
            rm -f "$base.out"
        fi
        expected_out="$tmp/$name.out"
        echo "updated $name.out"
    fi

    if [ "$status" != "${expected_status:-0}" ]; then
        fail "$name" "exit status $status, expected ${expected_status:-0}: $(head -n 1 "$tmp/$name.err")"
    elif ! cmp -s "$expected_out" "$tmp/$name.out"; then
        fail "$name" "unexpected output"
        diff "$expected_out" "$tmp/$name.out" | cat -v | head -n 20
    elif [ -n "$expected_stderr" ] && ! grep -qF -- "$expected_stderr" "$tmp/$name.err"; then
        fail "$name" "stderr lacks '$expected_stderr': $(head -n 1 "$tmp/$name.err")"
    else
        passed=$((passed + 1))
    fi
done

for src in "$root"/tests/errors/*.asm; do
    [ -e "$src" ] || continue
    name=$(basename "$src" .asm)
    wanted "$name" || continue
    expected=$(expectation "$src" expect-error)
    asm_args=$(expectation "$src" asm-args)

    if "$asm" $asm_args "$src" -o "$tmp/$name.bin" 2> "$tmp/$name.log"; then
        fail "$name" "assembled although it should not"
    elif ! grep -qF -- "$expected" "$tmp/$name.log"; then
        fail "$name" "expected '$expected', got: $(head -n 1 "$tmp/$name.log")"
    else
        passed=$((passed + 1))
    fi
done

echo "$passed passed, $failed failed"
[ "$failed" -eq 0 ]
