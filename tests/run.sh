#!/bin/sh
# Usage: tests/run.sh [-u] [NAME...]
# Runs tests/programs/*.asm and ore/tests/*.ore on the VM and checks that tests/errors/*.asm fail to
# assemble and ore/tests/errors/*.ore fail to compile, or only the tests named. Ore tests are compiled
# with vmc, the Ore compiler written in Ore, and with vmc0, and both builds have to meet the
# expectations; the errors of an Ore program are compared by message and Ore line. vmc1.bin, the build
# of vmc that vmc0 made, has to write the same assembly as vmc, all three compilers the same errors, and
# the test named vmc checks that vmc compiles itself into vmc.bin. Expectations come from NAME.out,
# NAME.in and "; expect-exit:", "; expect-stderr:", "; expect-error:", "; expect-warning:",
# "; asm-args:", "; ld-args:", "; vm-args:" and "; program-args:" lines, written with // in Ore, where
# "// vmc-args:" gives vmc0 more arguments.
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

# Compiles an Ore test with vmc into $tmp/NAME.asm, and with vmc1.bin and vmc0 as well, which have to
# agree with it. Leaves the status of vmc in $status and its errors in $tmp/NAME.log.
compile_ore() {
    name=$1
    shift
    (cd "$root" && "$vm" -m 262144 -S 8192 ./vmc.bin -S "$@" -o "$tmp/$name.asm" 2> "$tmp/$name.log")
    status=$?
    (cd "$root" && "$vm" -m 262144 -S 8192 ./vmc1.bin -S "$@" -o "$tmp/$name.stage1.asm" 2> "$tmp/$name.stage1.log")
    stage1=$?
    (cd "$root" && ./vmc0 -S "$@" -o "$tmp/$name.vmc0.asm" 2> "$tmp/$name.vmc0.log")
    vmc0=$?
    sed 's/^vmc0: /vmc: /' "$tmp/$name.vmc0.log" > "$tmp/$name.vmc0.err"
    if [ "$stage1" -ne "$status" ] || ! cmp -s "$tmp/$name.log" "$tmp/$name.stage1.log"; then
        fail "$name" "vmc1.bin disagrees with vmc: $(head -n 1 "$tmp/$name.stage1.log")"
        return 1
    elif [ "$vmc0" -ne "$status" ] || ! cmp -s "$tmp/$name.log" "$tmp/$name.vmc0.err"; then
        fail "$name" "vmc0 disagrees with vmc: $(head -n 1 "$tmp/$name.vmc0.log")"
        return 1
    elif [ "$status" -eq 0 ] && ! cmp -s "$tmp/$name.stage1.asm" "$tmp/$name.asm"; then
        fail "$name" "vmc1.bin writes other assembly than vmc"
        diff "$tmp/$name.stage1.asm" "$tmp/$name.asm" | head -n 10
        return 1
    fi
}

# Runs DIR/NAME.bin as the test asks, from DIR so the files it creates stay there, and leaves its exit
# status in $status. The instruction limit turns a runaway program into a failure instead of a hang.
execute() {
    dir=$1
    name=$2
    base=$3
    src=$4
    input=/dev/null
    [ -f "$base.in" ] && input="$base.in"
    args=$(expectation "$src" vm-args)
    program_args=$(expectation "$src" program-args)
    if [ -f "$base.x" ]; then
        cp "$base.x" "$dir/$name.x"
        args="$args -x $name.x"
    fi
    if [ -f "$base.keys" ]; then
        cp "$base.keys" "$dir/$name.keys"
        args="$args -k $name.keys"
    fi
    sectors=$(expectation "$src" disk-sectors)
    if [ -n "$sectors" ]; then
        dd if=/dev/zero of="$dir/$name.img" bs=512 count="$sectors" 2> /dev/null
        args="$args -b $name.img"
    fi
    (cd "$dir" && "$vm" -n 10000000 $args "$name.bin" $program_args < "$input" > "$name.out" 2> "$name.err")
    status=$?
}

# Errors of Ore programs are compared by message and source line, since the code addresses, labels and
# instructions around them depend on the compiler, and so do heap addresses, as the heap follows the code
normalize() {
    sed -E -e 's/0x[0-9A-F]+ <[^>]*> //' -e 's/(\.ore:[0-9]+): .*/\1/' -e 's/0x[0-9A-Fa-f]+/0x?/g' "$1"
}

# Compares the run in DIR with the expectations of its source, reporting a failure as NAME
check_run() {
    dir=$1
    name=$2
    base=$3
    src=$4
    expected_status=$(expectation "$src" expect-exit)
    expected_stderr=$(expectation "$src" expect-stderr)
    expected_out="$base.out"
    if [ ! -f "$expected_out" ]; then
        expected_out="$tmp/empty"
        : > "$expected_out"
    fi
    expected_err="$base.err"
    actual_err="$dir/$(basename "$name" " (vmc0)").err"
    if [ -f "$expected_err" ] && [ "${src%.ore}" != "$src" ]; then
        normalize "$expected_err" > "$dir/expected.err"
        normalize "$actual_err" > "$dir/actual.err"
        expected_err="$dir/expected.err"
        actual_err="$dir/actual.err"
    fi
    actual_out="$dir/$(basename "$name" " (vmc0)").out"

    if [ "$status" != "${expected_status:-0}" ]; then
        fail "$name" "exit status $status, expected ${expected_status:-0}: $(head -n 1 "$actual_err")"
    elif ! cmp -s "$expected_out" "$actual_out"; then
        fail "$name" "unexpected output"
        diff "$expected_out" "$actual_out" | cat -v | head -n 20
    elif [ -n "$expected_stderr" ] && ! grep -qF -- "$expected_stderr" "$actual_err"; then
        fail "$name" "stderr lacks '$expected_stderr': $(head -n 1 "$actual_err")"
    elif [ -f "$base.err" ] && ! cmp -s "$expected_err" "$actual_err"; then
        fail "$name" "unexpected stderr"
        diff "$expected_err" "$actual_err" | cat -v | head -n 20
    else
        return 0
    fi
    return 1
}

# Runs $tmp/NAME.bin and compares what it does with the expectations of its source, which -u rewrites
run_program() {
    name=$1
    base=$2
    src=$3
    execute "$tmp" "$name" "$base" "$src"
    expected_status=$(expectation "$src" expect-exit)
    if [ "$update" -eq 1 ] && [ "$status" = "${expected_status:-0}" ] && [ -f "$base.out" -o -s "$tmp/$name.out" ] &&
        ! cmp -s "$base.out" "$tmp/$name.out"; then
        if [ -s "$tmp/$name.out" ]; then
            cp "$tmp/$name.out" "$base.out"
        else
            rm -f "$base.out"
        fi
        echo "updated $name.out"
    fi
    if [ "$update" -eq 1 ] && [ "$status" = "${expected_status:-0}" ] && [ -f "$base.err" ] &&
        ! cmp -s "$base.err" "$tmp/$name.err"; then
        cp "$tmp/$name.err" "$base.err"
        echo "updated $name.err"
    fi
    check_run "$tmp" "$name" "$base" "$src"
}

for src in "$root"/tests/programs/*.asm; do
    [ -e "$src" ] || continue
    name=$(basename "$src" .asm)
    base=${src%.asm}
    wanted "$name" || continue

    # Assemble with a relative path so debug info does not depend on the checkout location
    asm_args=$(expectation "$src" asm-args)
    ld_args=$(expectation "$src" ld-args)
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
        if (cd "$root" && "$ld" $ld_args -o "$tmp/$name.bin" $objects 2> "$tmp/$name.link"); then
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

    run_program "$name" "$base" "$src" && passed=$((passed + 1))
done

mkdir -p "$tmp/vmc0"
for src in "$root"/ore/tests/*.ore; do
    [ -e "$src" ] || continue
    name=$(basename "$src" .ore)
    base=${src%.ore}
    wanted "$name" || continue

    compile_ore "$name" $(expectation "$src" vmc-args) "ore/tests/$name.ore" || continue
    if [ "$status" -ne 0 ]; then
        fail "$name" "does not compile: $(head -n 1 "$tmp/$name.log")"
        continue
    fi
    if ! (cd "$root" && "$asm" -I ./ore/lib -I . "$tmp/$name.asm" -o "$tmp/$name.bin" 2> "$tmp/$name.log" &&
        "$asm" -I ./ore/lib -I . "$tmp/$name.vmc0.asm" -o "$tmp/vmc0/$name.bin" 2> "$tmp/$name.log"); then
        fail "$name" "does not assemble: $(head -n 1 "$tmp/$name.log")"
        continue
    fi
    run_program "$name" "$base" "$src" || continue
    # A debugger script shows the code itself, which only the build of vmc matches
    if [ ! -f "$base.x" ]; then
        execute "$tmp/vmc0" "$name" "$base" "$src"
        check_run "$tmp/vmc0" "$name (vmc0)" "$base" "$src" || continue
    fi
    passed=$((passed + 1))
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

    compile_ore "$name" "ore/tests/errors/$name.ore" || continue
    if [ "$status" -eq 0 ]; then
        fail "$name" "compiled although it should not"
    elif ! grep -qF -- "$expected" "$tmp/$name.log"; then
        fail "$name" "expected '$expected', got: $(head -n 1 "$tmp/$name.log")"
    else
        passed=$((passed + 1))
    fi
done

# vmc compiles itself into itself
if wanted vmc; then
    if ! (cd "$root" && ./vmc ore/compiler/vmc.ore -o "$tmp/vmc.bin" 2> "$tmp/vmc.log"); then
        fail vmc "does not compile itself: $(head -n 1 "$tmp/vmc.log")"
    elif ! cmp -s "$root/vmc.bin" "$tmp/vmc.bin"; then
        fail vmc "compiles itself into another binary than vmc.bin"
    else
        passed=$((passed + 1))
    fi
fi

echo "$passed passed, $failed failed"
[ "$failed" -eq 0 ]
