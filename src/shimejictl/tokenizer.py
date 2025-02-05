from __future__ import annotations
import enum

TokenType = enum.Enum('TokenType', ["IDENTIFIER", "NUMBER", "OPENING_BRACKET", "CLOSING_BRACKET", "OPERATOR", "QUESTION_MARK", "COLON", "SEMICOLON", "EOF", "SOF", "ACCESS_OPERATOR", "COMMA", "INVALID"])
BracketType = enum.Enum('BracketType', ["EXPRESSION", "SCOPE", "ARRAY"])
OperatorType = enum.Enum('OperatorType', [
    "ADD", "SUBTRACT", "MULTIPLY", "DIVIDE", "MODULUS", "POWER",
    "BITWISE_AND", "BITWISE_OR", "BITWISE_XOR", "BITWISE_NOT", "LEFT_SHIFT", "RIGHT_SHIFT", "OR", "AND",
    "INCREMENT", "DECREMENT",
    "LESS_THAN", "LESS_THAN_OR_EQUAL", "GREATER_THAN", "GREATER_THAN_OR_EQUAL", "EQUAL", "NOT_EQUAL", "NOT", "INVALID"
])

ALLOWED_IDENTIFIERS = set("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_")
ALLOWED_NUMBERS = set("0123456789")
ALLOWED_OPERATORS = set("+-*/%&|^~<>=!")
ALLOWED_BRACKETS = set("()[]{}")
ALLOWED_WHITESPACE = set(" \t\n")
ALLOWED_ACCESS_OPERATOR = set(".")
ALLOWED_QUESTION_MARK = set("?")
ALLOWED_COLON = set(":")
ALLOWED_SEMICOLON = set(";")
ALLOWED_COMMA = set(",")


OPERATOR_MAP = {
    "+": OperatorType.ADD,
    "-": OperatorType.SUBTRACT,
    "*": OperatorType.MULTIPLY,
    "/": OperatorType.DIVIDE,
    "%": OperatorType.MODULUS,
    "&": OperatorType.BITWISE_AND,
    "|": OperatorType.BITWISE_OR,
    "^": OperatorType.BITWISE_XOR,
    "~": OperatorType.BITWISE_NOT,
    "<<": OperatorType.LEFT_SHIFT,
    ">>": OperatorType.RIGHT_SHIFT,
    "<": OperatorType.LESS_THAN,
    "<=": OperatorType.LESS_THAN_OR_EQUAL,
    ">": OperatorType.GREATER_THAN,
    ">=": OperatorType.GREATER_THAN_OR_EQUAL,
    "==": OperatorType.EQUAL,
    "!=": OperatorType.NOT_EQUAL,
    "**": OperatorType.POWER,
    "!": OperatorType.NOT,
    "||": OperatorType.OR,
    "&&": OperatorType.AND,
    "++": OperatorType.INCREMENT,
    "--": OperatorType.DECREMENT,

}

BRACKET_MAP = {
    "(": BracketType.EXPRESSION,
    ")": BracketType.EXPRESSION,
    "[": BracketType.ARRAY,
    "]": BracketType.ARRAY,
    "{": BracketType.SCOPE,
    "}": BracketType.SCOPE,
}

OPENING_BRACKETS = set("([{")
CLOSING_BRACKETS = set(")]}")

def prepare_string(js_string: str) -> str:
    if (js_string.startswith("${") or js_string.startswith("#{") ) and js_string.endswith("}"):
        js_string = js_string[2:-1]
    js_string = js_string.replace("&lt;", "<")\
    .replace("&gt;", ">").replace("&amp;", "&")\
    .replace("&quot;", "\"").replace("&apos;", "'")\
    .replace("&nbsp;", " ").replace("&copy;", "©")\
    .replace("&reg;", "®").replace("&trade;", "™")\
    .replace("&euro;", "€").replace("&pound;", "£")
    return js_string

class Tokenizer:
    class Token:
        type: TokenType = None
        subtype: BracketType | OperatorType = None
        value: str = None
        start: tuple[int,int] = None
        end: tuple[int,int] = None

        invalid_by: str = None

        def __init__(self, *args, **kwargs) -> None:
            x = ["type", "subtype", "value", "start", "end", "invalid_by"]

            for k,v in kwargs.items():
                if k in x:
                    setattr(self, k, v)
                x.remove(k)

            for arg in args:
                setattr(self, x.pop(0), arg)

    @staticmethod
    def __prepare_string(js_string: str) -> str:
        if (js_string.startswith("${") or js_string.startswith("#{") ):
            if js_string.endswith("}"):
                js_string = js_string[2:-1]
            else:
                raise SyntaxError("Invalid string interpolation")

        js_string = js_string.lower().replace("&lt;", "<")\
        .replace("&gt;", ">").replace("&amp;", "&")\
        .replace("&quot;", "\"").replace("&apos;", "'")\
        .replace("&nbsp;", " ").replace("&copy;", "©")\
        .replace("&reg;", "®").replace("&trade;", "™")\
        .replace("&euro;", "€").replace("&pound;", "£")\
        .replace("math.random*", "math.random()*")\
        .replace("math.random/", "math.random()/")\
        .replace("math.random-", "math.random()-")\
        .replace("math.random+", "math.random()+")

        return js_string

    @staticmethod
    def tokenize(string: str) -> list[Token]:

        string = Tokenizer.__prepare_string(string)

        tokens: list[Tokenizer.Token] = []
        line, col = 0,0
        current_token = Tokenizer.Token()

        for char in string:
            if char == '\n':
                line += 1
                col = 0

            if current_token.type is None:
                if char not in ALLOWED_WHITESPACE:
                    current_token = Tokenizer.__initialize_token_by_char(char, line, col)
            else:
                if char in ALLOWED_WHITESPACE:
                    tokens.append(Tokenizer.__emit_token(current_token, line, col))
                    current_token = Tokenizer.Token()

                elif current_token.type == TokenType.IDENTIFIER:
                    if char in ALLOWED_IDENTIFIERS or char in ALLOWED_NUMBERS:
                        current_token.value += char

                    else:
                        tokens.append(Tokenizer.__emit_token(current_token, line, col))
                        current_token = Tokenizer.__initialize_token_by_char(char, line, col)

                elif current_token.type == TokenType.NUMBER:
                    if char in ALLOWED_NUMBERS or char == ".":
                        current_token.value += char

                    elif char in ALLOWED_IDENTIFIERS:
                        current_token.type = TokenType.INVALID
                        current_token.invalid_by = f"Invalid character {char} in number"

                    else:
                        tokens.append(Tokenizer.__emit_token(current_token, line, col))
                        current_token = Tokenizer.__initialize_token_by_char(char, line, col)

                elif current_token.type == TokenType.OPERATOR:
                    if current_token.value + char in OPERATOR_MAP:
                        current_token.value += char

                    else:
                        tokens.append(Tokenizer.__emit_token(current_token, line, col))
                        current_token = Tokenizer.__initialize_token_by_char(char, line, col)

                elif current_token.type == TokenType.OPENING_BRACKET or current_token.type == TokenType.CLOSING_BRACKET:
                    tokens.append(Tokenizer.__emit_token(current_token, line, col))
                    current_token.subtype = BRACKET_MAP[current_token.value]
                    current_token = Tokenizer.__initialize_token_by_char(char, line, col)

                else:
                    tokens.append(Tokenizer.__emit_token(current_token, line, col))
                    current_token = Tokenizer.__initialize_token_by_char(char, line, col)

            col += 1

        if current_token.type is not None:
            tokens.append(Tokenizer.__emit_token(current_token, line, col))

        return tokens

    @staticmethod
    def __emit_token(current_token: Token, line: int, col: int) -> Token:
        if current_token.type == TokenType.OPERATOR:
            current_token.subtype = OPERATOR_MAP.get(current_token.value)
            if current_token.subtype is None:
                current_token.type = TokenType.INVALID
                current_token.invalid_by = f"Invalid operator {current_token.value}"

        elif current_token.type == TokenType.OPENING_BRACKET or current_token.type == TokenType.CLOSING_BRACKET:
            current_token.subtype = BRACKET_MAP[current_token.value]

        current_token.end = (line, col)
        return current_token

    @staticmethod
    def __initialize_token_by_char(char: str, line: int, col: int) -> Token:
        current_token = Tokenizer.Token()
        if char in ALLOWED_IDENTIFIERS:
            current_token.type = TokenType.IDENTIFIER
            current_token.value = char
            current_token.start = (line, col)

        elif char in ALLOWED_NUMBERS:
            current_token.type = TokenType.NUMBER
            current_token.value = char
            current_token.start = (line, col)

        elif char in ALLOWED_OPERATORS:
            current_token.type = TokenType.OPERATOR
            current_token.value = char
            current_token.start = (line, col)

        elif char in ALLOWED_BRACKETS:
            current_token.type = TokenType.OPENING_BRACKET if char in OPENING_BRACKETS else TokenType.CLOSING_BRACKET
            current_token.subtype = BRACKET_MAP[char]
            current_token.value = char
            current_token.start = (line, col)

        elif char in ALLOWED_ACCESS_OPERATOR:
            current_token.type = TokenType.ACCESS_OPERATOR
            current_token.value = char
            current_token.start = (line, col)

        elif char in ALLOWED_QUESTION_MARK:
            current_token.type = TokenType.QUESTION_MARK
            current_token.value = char
            current_token.start = (line, col)

        elif char in ALLOWED_COLON:
            current_token.type = TokenType.COLON
            current_token.value = char
            current_token.start = (line, col)

        elif char in ALLOWED_SEMICOLON:
            current_token.type = TokenType.SEMICOLON
            current_token.value = char
            current_token.start = (line, col)

        elif char in ALLOWED_COMMA:
            current_token.type = TokenType.COMMA
            current_token.value = char
            current_token.start = (line, col)

        return current_token
