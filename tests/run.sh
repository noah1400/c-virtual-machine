#!/bin/sh
# Runs tests/programs/*.asm on the VM and checks that tests/errors/*.asm fail to assemble.
# Expectations come from NAME.out, NAME.in and "; expect-exit:", "; expect-stderr:", "; expect-error:"
# and "; vm-args:" lines.

root=$(cd "$(dirname "$0")/.." && pwd)
vm="$root/vm"
asm="$root/vmasm"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT INT TERM
passed=0
failed=0

fail() {
    echo "FAIL $1: $2"
    failed=$((failed + 1))
}

expectation() {
    sed -n "s/^; $2: //p" "$1" | head -n 1
}

for src in "$root"/tests/programs/*.asm; do
    [ -e "$src" ] || continue
    name=$(basename "$src" .asm)
    base=${src%.asm}

    # Assemble with a relative path so debug info does not depend on the checkout location
    if ! (cd "$root" && "$asm" "tests/programs/$name.asm" -o "$tmp/$name.bin" 2> "$tmp/$name.log"); then
        fail "$name" "does not assemble: $(head -n 1 "$tmp/$name.log")"
        continue
    fi

    input=/dev/null
    [ -f "$base.in" ] && input="$base.in"
    # Programs run inside the scratch directory so the files they create do not leak
    args=$(expectation "$src" vm-args)
    (cd "$tmp" && "$vm" $args "$name.bin" < "$input" > "$name.out" 2> "$name.err")
    status=$?

    expected_status=$(expectation "$src" expect-exit)
    expected_stderr=$(expectation "$src" expect-stderr)
    expected_out="$base.out"
    if [ ! -f "$expected_out" ]; then
        expected_out="$tmp/empty"
        : > "$expected_out"
    fi

    if [ "$status" != "${expected_status:-0}" ]; then
        fail "$name" "exit status $status, expected ${expected_status:-0}: $(head -n 1 "$tmp/$name.err")"
    elif ! cmp -s "$expected_out" "$tmp/$name.out"; then
        fail "$name" "unexpected output"
        diff "$expected_out" "$tmp/$name.out" | head -n 20
    elif [ -n "$expected_stderr" ] && ! grep -qF -- "$expected_stderr" "$tmp/$name.err"; then
        fail "$name" "stderr lacks '$expected_stderr': $(head -n 1 "$tmp/$name.err")"
    else
        passed=$((passed + 1))
    fi
done

for src in "$root"/tests/errors/*.asm; do
    [ -e "$src" ] || continue
    name=$(basename "$src" .asm)
    expected=$(expectation "$src" expect-error)

    if "$asm" "$src" -o "$tmp/$name.bin" 2> "$tmp/$name.log"; then
        fail "$name" "assembled although it should not"
    elif ! grep -qF -- "$expected" "$tmp/$name.log"; then
        fail "$name" "expected '$expected', got: $(head -n 1 "$tmp/$name.log")"
    else
        passed=$((passed + 1))
    fi
done

echo "$passed passed, $failed failed"
[ "$failed" -eq 0 ]
