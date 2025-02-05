

from tokenizer import Tokenizer
from astnode import AbstractSyntaxTree
from bytecode import Program, OP
import os
import pathlib
import json
import shutil
import struct
from converter import *

OPCODE_TO_STR = {
    OP.ERR: "00",
    OP.RET: "01",
    OP.LOADL: "10", # IMMEDIATE ARGUMENT 1 BYTE INDEX
    OP.LOADE: "11", # IMMEDIATE ARGUMENT 1 BYTE INDEX
    OP.STORE: "12", # IMMEDIATE ARGUMENT 4 BYTE LE FLOAT
    OP.ADD: "20",
    OP.SUB: "21",
    OP.MUL: "22",
    OP.DIV: "23",
    OP.MOD: "24",
    OP.POW: "25",
    OP.AND: "30",
    OP.OR: "31",
    OP.XOR: "32",
    OP.NOT: "33",
    OP.LSHIFT: "34",
    OP.RSHIFT: "35",
    OP.LT: "40",
    OP.LE: "41",
    OP.GT: "42",
    OP.GE: "43",
    OP.EQ: "44",
    OP.NE: "45",
    OP.LAND: "50",
    OP.LOR: "51",
    OP.LNOT: "52",
    OP.BQZ: "60", # IMMEDIATE ARGUMENT 1 BYTE OFFSET
    OP.BNZ: "61", # IMMEDIATE ARGUMENT 1 BYTE OFFSET
    OP.JMP: "62", # IMMEDIATE ARGUMENT 1 BYTE OFFSET
    OP.CALL: "70" # IMMEDIATE ARGUMENT 1 BYTE INDEX
}

def bytes_to_hex(b: bytes) -> str:
    return ''.join(f"{byte:02x}".upper() for byte in b)

def float_to_hex(f: float) -> str:
    bytes = struct.pack('<f', float(f))
    return bytes_to_hex(bytes)

def int_to_hex(i: int) -> str:
    bytes = struct.pack('<I', i)
    return bytes_to_hex(bytes)


def char_to_hex(c: int) -> str:
    bytes = struct.pack('<B', c)
    return bytes_to_hex(bytes)

class Compiler:
    @staticmethod
    def emit(program: Program, json_kwargs: dict = {}) -> dict:
        data = {
            "instructions": "",
            "local_vars": program.local_vars,
            "global_vars": program.global_vars,
            "functions": program.functions
        }

        try:
            for index, instruction in enumerate(program.instructions):
                data["instructions"] += OPCODE_TO_STR[instruction[0]]
                if instruction[0] in [OP.LOADL, OP.LOADE, OP.CALL]:
                    data["instructions"] += char_to_hex(instruction[1])
                elif instruction[0] in [OP.STORE]:
                    hexdata = float_to_hex(instruction[1])
                    data["instructions"] += hexdata[:2]
                    data["instructions"] += "13"
                    data["instructions"] += hexdata[2:4]
                    data["instructions"] += "14"
                    data["instructions"] += hexdata[4:6]
                    data["instructions"] += "15"
                    data["instructions"] += hexdata[6:]
                    data["instructions"] += "8000"
                elif instruction[0] in [OP.BQZ, OP.BNZ, OP.JMP]:
                    # We need to calculate the offset
                    offset = 0
                    for ofinstr in program.instructions[index+1:index+instruction[1]]:
                        if ofinstr[0] in [OP.STORE]:
                            offset += 2 # First byte
                            offset += 2 # Second byte
                            offset += 2 # Third byte
                            offset += 2 # Fourth byte
                            offset += 2 # Push
                        else:
                            offset += 2

                    data["instructions"] += char_to_hex(offset)
                else:
                    data["instructions"] += "00"
        except:
            print(f"Error at instruction {index}: {instruction}")
            raise

        return data


    @staticmethod
    def compile(expression: str, locals: list = [], globals: list = [], funcs: list = []) -> Program:
        tokens = Tokenizer.tokenize(expression)
        root_node = AbstractSyntaxTree(tokens).parse()
        program = Program.from_ast(root_node, locals, globals, funcs)
        if expression.startswith("#{"):
            program.evaluate_once = False
        return program

    @staticmethod
    def compile_shimeji(root_dir: str | pathlib.Path, **kwargs) -> tuple[str, str, str]:

        """
        root_dir: str | pathlib.Path
            The root directory of the mascot
        returns:
            programs: str - json serialized programs
            actions: str - json serialized actions
            behaviors: str - json serialized behaviors
            images: list[str] - list of image paths
        """

        conf_path = pathlib.Path(kwargs.get("conf_path", pathlib.Path(root_dir) / "conf"))

        # Is root_dir exists?
        if not pathlib.Path(root_dir).exists():
            raise FileNotFoundError(f"Directory {root_dir} does not exist.")

        if not pathlib.Path(root_dir).is_dir():
            raise NotADirectoryError(f"{root_dir} is not a directory.")

        if not conf_path.exists():
            raise FileNotFoundError(f"Directory {root_dir} does not contain a 'conf' directory.")

        if not conf_path.is_dir():
            raise NotADirectoryError(f"{root_dir}/conf is not a directory.")

        if not (conf_path / "actions.xml").exists():
            raise FileNotFoundError(f"Directory {root_dir}/conf does not contain an 'actions.xml' file.")

        if not (conf_path / "actions.xml").is_file():
            raise FileNotFoundError(f"{root_dir}/conf/actions.xml is not a file.")

        if not (conf_path / "behaviors.xml").exists():
            raise FileNotFoundError(f"Directory {root_dir}/conf does not contain a 'behaviors.xml' file.")

        if not (conf_path / "behaviors.xml").is_file():
            raise FileNotFoundError(f"{root_dir}/conf/behaviors.xml is not a file.")

        with open(conf_path / "actions.xml", "r") as f:
            actions = f.read()

        with open(conf_path / "behaviors.xml", "r") as f:
            behaviors = f.read()

        programs_defintions, actions_defintions, (behaviors_defintions, root_behaviors) = shmconv(actions, behaviors)

        programs = {
            "programs": []
        }
        for index, program_definition in enumerate(programs_defintions):
            program = Compiler.compile(program_definition, [], [], [])
            program_serialized = Compiler.emit(program)

            # print("original", program_definition)
            # print(f"locals {program.local_vars}")

            programs["programs"].append({
                "name": index,
                "symtab_l": program.local_vars,
                "symtab_g": program.global_vars,
                "symtab_f": program.functions,
                "instructions": program_serialized["instructions"],
                "evaluate_once": program.evaluate_once
            })

        actions = [
            action_definition for action_definition in actions_defintions.values()
        ]

        behaviors = {
            "definitions": [x for x in behaviors_defintions.values()],
            "root_behavior_list": root_behaviors
        }

        return json.dumps(programs, indent=kwargs.get("indent", 4)), json.dumps(actions, indent=kwargs.get("indent", 4)), json.dumps(behaviors, indent=kwargs.get("indent", 4))
