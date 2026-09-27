#!/bin/sh
# Usage: tests/run.sh [-u] [NAME...]
# Runs tests/programs/*.asm and ore/tests/*.ore on the VM and checks that tests/errors/*.asm fail to
# assemble and ore/tests/errors/*.ore fail to compile, or only the tests named. vmc, the Ore compiler
# written in Ore, has to write the same assembly and errors as vmc0 for every Ore test, and the test
# named vmc checks that it compiles itself into vmc.bin. Expectations come from NAME.out,
# NAME.in and "; expect-exit:", "; expect-stderr:", "; expect-error:", "; expect-warning:",
# "; asm-args:", "; vm-args:" and "; program-args:" lines, written with // in Ore, where "// vmc-args:"
# gives vmc0 more arguments.
# Programs are assembled with -W, and every warning needs an "; expect-warning:" line of its own.
# "; disk-sectors:" attaches an empty disk image, NAME.x holds debugger commands, NAME.keys a key script
# and NAME.err, if present, the whole expected stderr. "; link:" names modules in tests/programs to
# assemble with -c and link with the program, and "; expect-link-error:" a message the link must fail
# with. -u rewrites NAME.out and an existing NAME.err from the actual output of every program that
# exits as expected.

root=$(cd "$(dirname "$0")/.." && pwd)
vm="$root/vm"
asm="$root/vmasm"
ld="$root/vmld"
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
    [ "$name" = vmc ] || [ -f "$root/tests/programs/$name.asm" ] || [ -f "$root/tests/errors/$name.asm" ] ||
        [ -f "$root/ore/tests/$name.ore" ] || [ -f "$root/ore/tests/errors/$name.ore" ] ||
        fail "$name" "there is no such test"
done

# Assembly tests write their expectations in ; comments, Ore tests in // comments
expectation() {
    sed -n -e "s/^; $2: //p" -e "s|^// $2: ||p" "$1" | head -n 1
}

# Compiles with vmc0 and vmc, the Ore compiler written in Ore, which have to agree on the assembly they
# write after its first line, which names the compiler, and on their errors. Leaves the status of vmc0
# in $status and its errors in $tmp/NAME.log.
same_as_vmc0() {
    name=$1
    shift
    (cd "$root" && ./vmc0 -S "$@" -o "$tmp/$name.vmc0.asm" 2> "$tmp/$name.log")
    status=$?
    (cd "$root" && ./vmc -S "$@" -o "$tmp/$name.vmc.asm" 2> "$tmp/$name.vmc.log")
    vmc_status=$?
    sed 's/^vmc: /vmc0: /' "$tmp/$name.vmc.log" > "$tmp/$name.vmc.err"
    if [ "$vmc_status" -ne "$status" ] || ! cmp -s "$tmp/$name.log" "$tmp/$name.vmc.err"; then
        fail "$name" "vmc disagrees with vmc0: $(head -n 1 "$tmp/$name.vmc.log")"
        return 1
    fi
    if [ "$status" -eq 0 ]; then
        tail -n +2 "$tmp/$name.vmc0.asm" > "$tmp/$name.expected.asm"
        tail -n +2 "$tmp/$name.vmc.asm" > "$tmp/$name.actual.asm"
        if ! cmp -s "$tmp/$name.expected.asm" "$tmp/$name.actual.asm"; then
            fail "$name" "vmc writes other assembly than vmc0"
            diff "$tmp/$name.expected.asm" "$tmp/$name.actual.asm" | head -n 10
            return 1
        fi
    fi
}

# Runs $tmp/NAME.bin and compares what it does with the expectations of its source
run_program() {
    name=$1
    base=$2
    src=$3
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
    if [ -f "$base.keys" ]; then
        cp "$base.keys" "$tmp/$name.keys"
        args="$args -k $name.keys"
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
    if [ "$update" -eq 1 ] && [ "$status" = "${expected_status:-0}" ] && [ -f "$base.err" ] &&
        ! cmp -s "$base.err" "$tmp/$name.err"; then
        cp "$tmp/$name.err" "$base.err"
        echo "updated $name.err"
    fi

    if [ "$status" != "${expected_status:-0}" ]; then
        fail "$name" "exit status $status, expected ${expected_status:-0}: $(head -n 1 "$tmp/$name.err")"
    elif ! cmp -s "$expected_out" "$tmp/$name.out"; then
        fail "$name" "unexpected output"
        diff "$expected_out" "$tmp/$name.out" | cat -v | head -n 20
    elif [ -n "$expected_stderr" ] && ! grep -qF -- "$expected_stderr" "$tmp/$name.err"; then
        fail "$name" "stderr lacks '$expected_stderr': $(head -n 1 "$tmp/$name.err")"
    elif [ -f "$base.err" ] && ! cmp -s "$base.err" "$tmp/$name.err"; then
        fail "$name" "unexpected stderr"
        diff "$base.err" "$tmp/$name.err" | cat -v | head -n 20
    else
        passed=$((passed + 1))
    fi
}

for src in "$root"/tests/programs/*.asm; do
    [ -e "$src" ] || continue
    name=$(basename "$src" .asm)
    base=${src%.asm}
    wanted "$name" || continue

    # Assemble with a relative path so debug info does not depend on the checkout location
    asm_args=$(expectation "$src" asm-args)
    modules=$(expectation "$src" link)
    link_error=$(expectation "$src" expect-link-error)
    output="$tmp/$name.bin"
    if [ -n "$modules$link_error" ]; then
        asm_args="$asm_args -c"
        output="$tmp/$name.o"
    fi
    if ! (cd "$root" && "$asm" -W $asm_args "tests/programs/$name.asm" -o "$output" 2> "$tmp/$name.log"); then
        fail "$name" "does not assemble: $(head -n 1 "$tmp/$name.log")"
        continue
    fi
    objects="$output"
    for module in $modules; do
        object="$tmp/$name.$(basename "$module" .asm).o"
        objects="$objects $object"
        if ! (cd "$root" && "$asm" -W -c "tests/programs/$module" -o "$object" 2>> "$tmp/$name.log"); then
            fail "$name" "$module does not assemble: $(tail -n 1 "$tmp/$name.log")"
            continue 2
        fi
    done
    warnings=$(grep -c ": warning: " "$tmp/$name.log")
    expected_warnings=$(sed -n "s/^; expect-warning: //p" "$src")
    missing=$(printf '%s\n' "$expected_warnings" | while IFS= read -r warning; do
        [ -z "$warning" ] || grep -qF -- "$warning" "$tmp/$name.log" || echo "$warning"
    done)
    if [ -n "$missing" ]; then
        fail "$name" "no warning '$(printf '%s\n' "$missing" | head -n 1)'"
        continue
    elif [ "$warnings" -ne "$(printf '%s' "$expected_warnings" | grep -c .)" ]; then
        fail "$name" "unexpected warning: $(grep ": warning: " "$tmp/$name.log" | head -n 1)"
        continue
    fi

    if [ -n "$modules$link_error" ]; then
        if (cd "$root" && "$ld" -o "$tmp/$name.bin" $objects 2> "$tmp/$name.link"); then
            if [ -n "$link_error" ]; then
                fail "$name" "linked although it should not"
                continue
            fi
        elif [ -n "$link_error" ] && grep -qF -- "$link_error" "$tmp/$name.link"; then
            passed=$((passed + 1))
            continue
        else
            fail "$name" "does not link: $(head -n 1 "$tmp/$name.link")"
            continue
        fi
    fi

    run_program "$name" "$base" "$src"
done

for src in "$root"/ore/tests/*.ore; do
    [ -e "$src" ] || continue
    name=$(basename "$src" .ore)
    base=${src%.ore}
    wanted "$name" || continue

    if ! (cd "$root" && ./vmc0 $(expectation "$src" vmc-args) "ore/tests/$name.ore" -o "$tmp/$name.bin" \
        2> "$tmp/$name.log"); then
        fail "$name" "does not compile: $(head -n 1 "$tmp/$name.log")"
        continue
    fi
    same_as_vmc0 "$name" $(expectation "$src" vmc-args) "ore/tests/$name.ore" || continue
    run_program "$name" "$base" "$src"
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

for src in "$root"/ore/tests/errors/*.ore; do
    [ -e "$src" ] || continue
    name=$(basename "$src" .ore)
    wanted "$name" || continue
    expected=$(expectation "$src" expect-error)

    same_as_vmc0 "$name" "ore/tests/errors/$name.ore" || continue
    if [ "$status" -eq 0 ]; then
        fail "$name" "compiled although it should not"
    elif ! grep -qF -- "$expected" "$tmp/$name.log"; then
        fail "$name" "expected '$expected', got: $(head -n 1 "$tmp/$name.log")"
    else
        passed=$((passed + 1))
    fi
done

# vmc compiles itself into the very binary that vmc0 made of it
if wanted vmc; then
    if ! (cd "$root" && ./vmc ore/compiler/vmc.ore -o "$tmp/vmc.bin" 2> "$tmp/vmc.log"); then
        fail vmc "does not compile itself: $(head -n 1 "$tmp/vmc.log")"
    elif ! cmp -s "$root/vmc.bin" "$tmp/vmc.bin"; then
        fail vmc "compiles itself into another binary than vmc0 does"
    else
        passed=$((passed + 1))
    fi
fi

echo "$passed passed, $failed failed"
[ "$failed" -eq 0 ]
