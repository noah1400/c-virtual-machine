#!/usr/bin/env python3
"""Runs the same programs written in C, Java, Ore and Python and compares how long they take.

Usage: bench/run.py [-n RUNS] [-k KERNEL,...] [-l LANGUAGE,...]

Every kernel exists once per language in bench/<language>. The script builds them into build/bench, checks
that all languages print the same output and prints the shortest wall-clock time of RUNS runs, starting the
program included. The empty kernel shows what starting a program costs. Languages whose tools are missing
are left out. Build the VM and vmc.bin with make first.
"""
import argparse
import os
import shutil
import statistics
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BENCH = os.path.join(ROOT, 'bench')
BUILD = os.path.join(ROOT, 'build', 'bench')
KERNELS = ['empty', 'fib', 'sieve', 'sort', 'matrix', 'hash', 'text']
LANGUAGES = ['c', 'java', 'ore', 'python']


def build(language, kernel):
    """Builds a kernel and returns the command that runs it."""
    if language == 'c':
        binary = os.path.join(BUILD, 'c_' + kernel)
        cc = os.environ.get('CC', 'cc')
        subprocess.run([cc, '-O2', '-o', binary, os.path.join(BENCH, 'c', kernel + '.c')], check=True)
        return [binary]
    if language == 'java':
        name = kernel.capitalize()
        classes = os.path.join(BUILD, 'java')
        subprocess.run(['javac', '-d', classes, os.path.join(BENCH, 'java', name + '.java')], check=True,
                       stderr=subprocess.DEVNULL)
        return ['java', '-cp', classes, name]
    if language == 'ore':
        binary = os.path.join(BUILD, 'ore_' + kernel + '.bin')
        subprocess.run([os.path.join(ROOT, 'vmc'), os.path.join(BENCH, 'ore', kernel + '.ore'), '-o', binary],
                       check=True)
        return [os.path.join(ROOT, 'vm'), '-m', '65536', binary]
    return [sys.executable, os.path.join(BENCH, 'python', kernel + '.py')]


def available(language):
    tools = {'c': [os.environ.get('CC', 'cc')], 'java': ['java', 'javac'], 'python': [],
             'ore': [os.path.join(ROOT, 'vm'), os.path.join(ROOT, 'vmasm')]}
    return all(shutil.which(tool) for tool in tools[language]) and \
        (language != 'ore' or os.path.exists(os.path.join(ROOT, 'vmc.bin')))


def run(command):
    """Runs a command and returns its output and wall-clock time in seconds."""
    start = time.perf_counter()
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, check=True)
    return result.stdout.decode(), time.perf_counter() - start


def main():
    parser = argparse.ArgumentParser(description='Compare C, Java, Ore and Python on the same programs')
    parser.add_argument('-n', type=int, default=5, help='runs of each program (default 5)')
    parser.add_argument('-k', default=','.join(KERNELS), help='kernels to run')
    parser.add_argument('-l', default=','.join(LANGUAGES), help='languages to run')
    args = parser.parse_args()
    kernels = args.k.split(',')
    languages = [language for language in args.l.split(',') if available(language)]
    os.makedirs(BUILD, exist_ok=True)

    commands = {(k, l): build(l, k) for k in kernels for l in languages}
    best = {}
    median = {}
    failed = False
    for kernel in kernels:
        outputs = {}
        for language in languages:
            times = []
            for _ in range(args.n):
                output, seconds = run(commands[kernel, language])
                times.append(seconds)
            outputs[language] = output
            best[kernel, language] = min(times)
            median[kernel, language] = statistics.median(times)
        if len(set(outputs.values())) > 1:
            failed = True
            print(f'{kernel}: the outputs differ', file=sys.stderr)
            for language, output in outputs.items():
                print(f'  {language}: {output.strip()}', file=sys.stderr)

    print(f'Shortest of {args.n} runs in seconds, and the median in brackets')
    print(f'{"":8}' + ''.join(f'{language:>18}' for language in languages))
    for kernel in kernels:
        cells = ''.join(f'{best[kernel, l]:>9.3f} ({median[kernel, l]:.3f})' for l in languages)
        print(f'{kernel:8}' + cells)
    if 'c' in languages and len(languages) > 1:
        print('\nTimes the shortest time of C')
        others = [language for language in languages if language != 'c']
        print(f'{"":8}' + ''.join(f'{language:>10}' for language in others))
        for kernel in kernels:
            if kernel != 'empty':
                print(f'{kernel:8}' + ''.join(f'{best[kernel, l] / best[kernel, "c"]:>10.1f}' for l in others))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
