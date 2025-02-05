from astnode import *
from tokenizer import Tokenizer, TokenType, OperatorType
import json
import enum

OP = enum.Enum("OP", [
    "ERR",
    "RET",
    # Stack section
    "LOADL",
    "LOADE",
    "STORE",
    # Arithmetic section
    "ADD",
    "SUB",
    "MUL",
    "DIV",
    "MOD",
    "POW",
    # Bitwise section
    "AND",
    "OR",
    "XOR",
    "NOT",
    "LSHIFT",
    "RSHIFT",
    # Comparison section
    "LT",
    "LE",
    "GT",
    "GE",
    "EQ",
    "NE",
    # Logical section
    "LAND",
    "LOR",
    "LNOT",
    # Control section
    # Branch
    "BQZ",
    "BNZ",
    "JMP",
    # Call
    "CALL"
])

FORCED_EXTERNAL = [
    "mascot.anchor",
    "mascot.totalcount",
    "mascot.count"
]
FORCED_LOCAL = [
    "Name",
    "Type",
    "Class",
    "Embedded",
    "Move",
    "Stay",
    "Animate",
    "Sequence",
    "Select",
    "BorderType",
    "Ceiling",
    "Wall",
    "Floor",
    "TargetX",
    "TargetY",
    "VelocityX",
    "VelocityY",
    "VelocityParam",
    "InitialVX",
    "InitialVY",
    "Gravity",
    "RegistanceX",
    "RegistanceY",
    "LookRight",
    "IeOffsetX",
    "IeOffsetY",
    "X",
    "Y",
    "BornX",
    "BornY",
    "BornBehaviour",
    "BornMascot",
    "BornInterval",
    "BornTransient",
    "BornCount",
    "TransformBehaviour",
    "TransformMascot",
    "Affordance",
    "Behaviour",
    "TargetBehaviour",
    "Loop",
    "Animation",
    "Condition",
    "Image",
    "ImageRight",
    "ImageAnchor",
    "Velocity",
    "Duration",
    "Draggable",
    "Sound",
    "Volume",
    "BehaviourList",
    "ChaseMouse",
    "Frequency",
    "Hidden",
    "NextBehaviourList",
    "Add",
    "BehaviourReference",
    "Fall",
    "Dragged",
    "Thrown",
    "FootX",
    "FootDX",
    "OffsetX",
    "OffsetY",
    "Pose",
    "Hotspot",
    "Shape",
    "Origin",
    "Size",
    "Constant",
    "Value",
    "IsTurn",
    "TargetLook",
    "Information",
    "PreviewImage",
    "SplashImage",
    "Artist",
    "Scripter",
    "URL",
    "Commissioner",
    "Support",
    "Toggleable",
    "Count",
    "LookRight",
    "Gap"
]

SYMNAME_REMAP = {x.lower(): f"mascot.{x[0].lower()}{x[1:]}" for x in FORCED_LOCAL}

class Program:
    def __init__(self):
        self.instructions = []
        self.local_vars = []
        self.global_vars = []
        self.functions = []
        self.evaluate_once = True

    @staticmethod
    def from_ast(ast: ASTNode, local_vars: list = [], global_vars: list = [], functions: list = []):
        program = Program()
        program.local_vars = local_vars
        program.global_vars = global_vars
        program.functions = functions
        program._compile(ast)
        program.instructions.append((OP.RET,))
        return program

    def _compile(self, node: ASTNode):
        if isinstance(node, LiteralNode):
            self.instructions.append((OP.STORE, node.value))
        elif isinstance(node, VariableNode):
            name = node.name
            symlist = self.local_vars if not name.lower() in FORCED_EXTERNAL else self.global_vars
            name = SYMNAME_REMAP.get(name.lower(), name)
            if name not in symlist:
                symlist.append(name)
            self.instructions.append((OP.LOADL, symlist.index(name)))

        elif isinstance(node, MemberAccessNode):
            name = str(node)
            name = SYMNAME_REMAP.get(name.lower(), name)
            if name.count(".") != 1 or name.lower() in FORCED_EXTERNAL and name not in FORCED_LOCAL:
                if name not in self.global_vars:
                    self.global_vars.append(name)
                self.instructions.append((OP.LOADE, self.global_vars.index(name)))
            else:
                if name.startswith("mascot.") or name in FORCED_LOCAL:
                    if name not in self.local_vars:
                        self.local_vars.append(name)
                    self.instructions.append((OP.LOADL, self.local_vars.index(name)))
                if name.startswith("math.") or name in FORCED_EXTERNAL:
                    if name not in self.global_vars:
                        self.global_vars.append(name)
                    self.instructions.append((OP.LOADE, self.global_vars.index(name)))
        elif isinstance(node, UnaryOpNode):
            if node.operator.subtype == OperatorType.NOT:
                self._compile(node.operand)
                self.instructions.append((OP.LNOT,))
            elif node.operator.subtype == OperatorType.SUBTRACT:
                self.instructions.append((OP.STORE, 0))
                self._compile(node.operand)
                self.instructions.append((OP.SUB,))
            elif node.operator.subtype == OperatorType.BITWISE_NOT:
                self._compile(node.operand)
                self.instructions.append((OP.NOT,))
            elif node.operator.subtype == OperatorType.ADD:
                self._compile(node.operand)
            else:
                raise Exception("Unknown unary operator")
        elif isinstance(node, BinaryOpNode):
            self._compile(node.left)
            self._compile(node.right)
            if node.operator.subtype == OperatorType.ADD:
                self.instructions.append((OP.ADD,))
            elif node.operator.subtype == OperatorType.SUBTRACT:
                self.instructions.append((OP.SUB,))
            elif node.operator.subtype == OperatorType.MULTIPLY:
                self.instructions.append((OP.MUL,))
            elif node.operator.subtype == OperatorType.DIVIDE:
                self.instructions.append((OP.DIV,))
            elif node.operator.subtype == OperatorType.MODULUS:
                self.instructions.append((OP.MOD,))
            elif node.operator.subtype == OperatorType.POWER:
                self.instructions.append((OP.POW,))
            elif node.operator.subtype == OperatorType.BITWISE_AND:
                self.instructions.append((OP.AND,))
            elif node.operator.subtype == OperatorType.BITWISE_OR:
                self.instructions.append((OP.OR,))
            elif node.operator.subtype == OperatorType.BITWISE_XOR:
                self.instructions.append((OP.XOR,))
            elif node.operator.subtype == OperatorType.LEFT_SHIFT:
                self.instructions.append((OP.LSHIFT,))
            elif node.operator.subtype == OperatorType.RIGHT_SHIFT:
                self.instructions.append((OP.RSHIFT,))
            elif node.operator.subtype == OperatorType.LESS_THAN:
                self.instructions.append((OP.LT,))
            elif node.operator.subtype == OperatorType.LESS_THAN_OR_EQUAL:
                self.instructions.append((OP.LE,))
            elif node.operator.subtype == OperatorType.GREATER_THAN:
                self.instructions.append((OP.GT,))
            elif node.operator.subtype == OperatorType.GREATER_THAN_OR_EQUAL:
                self.instructions.append((OP.GE,))
            elif node.operator.subtype == OperatorType.EQUAL:
                self.instructions.append((OP.EQ,))
            elif node.operator.subtype == OperatorType.NOT_EQUAL:
                self.instructions.append((OP.NE,))
            elif node.operator.subtype == OperatorType.AND:
                self.instructions.append((OP.LAND,))
            elif node.operator.subtype == OperatorType.OR:
                self.instructions.append((OP.LOR,))
            else:
                raise Exception("Unknown binary operator")

        elif isinstance(node, TernaryOpNode):
            self._compile(node.condition)
            branch_instr = len(self.instructions)
            self.instructions.append((OP.BQZ, 0))
            self._compile(node.true_expr)
            jump_instr = len(self.instructions)
            self.instructions.append((OP.JMP, 0))
            self.instructions[branch_instr] = (OP.BQZ, len(self.instructions)-branch_instr)
            self._compile(node.false_expr)
            self.instructions[jump_instr] = (OP.JMP, len(self.instructions)-jump_instr)


        elif isinstance(node, FunctionCallNode):
            name = str(node.function_name)
            for arg in node.arguments:
                self._compile(arg)
            if name not in self.functions:
                self.functions.append(name)
            self.instructions.append((OP.CALL, self.functions.index(name)))


    def __repr__(self) -> str:
        return f"<{self.__class__.__name__}: {len(self.instructions)} instructions long, located at 0x{id(self)}>"
