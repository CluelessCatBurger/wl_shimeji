"""
    Program to combine multiple python files into one file
"""

import os
import sys
import shutil
import pathlib

import argparse

argparser = argparse.ArgumentParser(description='Combine multiple python files into one file')
argparser.add_argument('file', type=str, nargs='+', help='Python files to combine')
argparser.add_argument('-o', '--output', type=str, help='Output file')
argparser.add_argument('-c', '--compile-to-bytecode', action='store_true', help='Compile the output file to bytecode')
argparser.add_argument('-s', '--shebang', action='store_true', help='Add shebang to the output file')

args = argparser.parse_args()

imports = set(
    [
        "from __future__ import annotations\n",
    ]
)

processed_files = set()
output_lines = []

def process_file(file_path, mainfile=True):
    path = pathlib.Path(file_path)
    if file_path in processed_files:
        return

    processed_files.add(file_path)

    with open(file_path, 'r') as f:
        lines = f.readlines()

    for line in lines:

        if line.startswith('if __name__ == "__main__":') and not mainfile:
            break

        stripped_line = line.strip()
        if stripped_line.startswith('#'):
            output_lines.append(line)
        if line.startswith('import ') or line.startswith('from '):
            ...  # Skip imports
        else:
            output_lines.append(line)

        if line.startswith('from ') and 'import' in line:
            module_name = stripped_line.split()[1]
            module_file = module_name.replace('.', '/') + '.py'
            if os.path.exists(path.parent / module_file):
                process_file(path.parent / module_file, False)
                output_lines.append('\n\n')
            else:
                imports.add(line)

        if line.startswith('import '):
            imports.add(line)

for file in args.file:
    process_file(file)

with open(args.output, 'w') as output_file:
    if args.shebang:
        output_file.write('#!/usr/bin/env python3\n\n')
    imports.remove('from __future__ import annotations\n')
    output_file.write('from __future__ import annotations\n')
    output_file.writelines(imports)
    output_file.write('\n')
    output_file.writelines(output_lines)

if args.compile_to_bytecode:
    import py_compile
    py_compile.compile(args.output, cfile=args.output + 'c')
