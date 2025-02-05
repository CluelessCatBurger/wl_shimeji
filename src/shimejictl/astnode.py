from tokenizer import Tokenizer, TokenType, OperatorType

class ASTNode:
    pass

class BinaryOpNode(ASTNode):
    def __init__(self, left, operator, right):
        self.left = left
        self.operator = operator
        self.right = right

    def __repr__(self):
        return f"({self.left} {self.operator} {self.right})"

class UnaryOpNode(ASTNode):
    def __init__(self, operator: Tokenizer.Token, operand: ASTNode):
        self.operator = operator
        self.operand = operand

    def __repr__(self):
        return f"({self.operator}{self.operand})"

class LiteralNode(ASTNode):
    def __init__(self, value):
        self.value = value

    def __repr__(self):
        return str(self.value)

class VariableNode(ASTNode):
    def __init__(self, name):
        self.name = name

    def __repr__(self):
        return self.name

class FunctionCallNode(ASTNode):
    def __init__(self, function_name, arguments):
        self.function_name = function_name
        self.arguments = arguments

    def __repr__(self):
        return f"{self.function_name}({', '.join(map(str, self.arguments))})"

class MemberAccessNode(ASTNode):
    def __init__(self, left, right):
        self.left = left
        self.right = right

    def __str__(self) -> str:
        return f"{self.left}.{self.right}"

    def __repr__(self):
        return f"<MemberAccessNode: {str(self)}>"

class TernaryOpNode(ASTNode):
    def __init__(self, condition, true_expr, false_expr):
        self.condition = condition
        self.true_expr = true_expr
        self.false_expr = false_expr

    def __repr__(self):
        return f"({self.condition} ? {self.true_expr} : {self.false_expr})"

class AbstractSyntaxTree:
    def __init__(self, tokens: list[Tokenizer.Token]):
        self.tokens = tokens
        self.pos = 0
        self.operator_stack = []
        self.operand_stack = []

    def current_token(self) -> Tokenizer.Token:
        if self.pos < len(self.tokens):
            return self.tokens[self.pos]
        return Tokenizer.Token(type=TokenType.EOF)

    def eat(self, token_type: TokenType) -> Tokenizer.Token:
        current = self.current_token()
        if current.type == token_type:
            self.pos += 1
            return current
        raise SyntaxError(f"Expected token {token_type}, but got {current.type}")

    def parse(self) -> ASTNode:
        return self.parse_expression()

    def parse_expression(self) -> ASTNode:
        return self.parse_ternary()

    def parse_ternary(self) -> ASTNode:
        condition = self.parse_logical_or()
        if self.current_token().type == TokenType.QUESTION_MARK:
            self.eat(TokenType.QUESTION_MARK)
            true_expr = self.parse_expression()
            self.eat(TokenType.COLON)
            false_expr = self.parse_expression()
            return TernaryOpNode(condition, true_expr, false_expr)
        return condition

    def parse_logical_or(self) -> ASTNode:
        node = self.parse_logical_and()
        while self.current_token().subtype == OperatorType.OR:
            operator = self.eat(TokenType.OPERATOR)
            right = self.parse_logical_and()
            node = BinaryOpNode(node, operator, right)
        return node

    def parse_logical_and(self) -> ASTNode:
        node = self.parse_equality()
        while self.current_token().subtype == OperatorType.AND:
            operator = self.eat(TokenType.OPERATOR)
            right = self.parse_equality()
            node = BinaryOpNode(node, operator, right)
        return node

    def parse_equality(self) -> ASTNode:
        node = self.parse_comparison()
        while self.current_token().subtype in (OperatorType.EQUAL, OperatorType.NOT_EQUAL):
            operator = self.eat(TokenType.OPERATOR)
            right = self.parse_comparison()
            node = BinaryOpNode(node, operator, right)
        return node

    def parse_comparison(self) -> ASTNode:
        node = self.parse_additive()
        while self.current_token().subtype in (OperatorType.LESS_THAN, OperatorType.LESS_THAN_OR_EQUAL, OperatorType.GREATER_THAN, OperatorType.GREATER_THAN_OR_EQUAL):
            operator = self.eat(TokenType.OPERATOR)
            right = self.parse_additive()
            node = BinaryOpNode(node, operator, right)
        return node

    def parse_additive(self) -> ASTNode:
        node = self.parse_multiplicative()
        while self.current_token().subtype in (OperatorType.ADD, OperatorType.SUBTRACT):
            operator = self.eat(TokenType.OPERATOR)
            right = self.parse_multiplicative()
            node = BinaryOpNode(node, operator, right)
        return node

    def parse_multiplicative(self) -> ASTNode:
        node = self.parse_unary()
        while self.current_token().subtype in (OperatorType.MULTIPLY, OperatorType.DIVIDE, OperatorType.MODULUS):
            operator = self.eat(TokenType.OPERATOR)
            right = self.parse_unary()
            node = BinaryOpNode(node, operator, right)
        return node

    def parse_unary(self) -> ASTNode:
        token = self.current_token()
        if token.subtype in (OperatorType.NOT, OperatorType.SUBTRACT, OperatorType.BITWISE_NOT):
            operator = self.eat(TokenType.OPERATOR)
            operand = self.parse_unary()
            return UnaryOpNode(operator, operand)
        return self.parse_primary()

    def parse_primary(self) -> ASTNode:
        token = self.current_token()
        if token.type == TokenType.NUMBER:
            self.eat(TokenType.NUMBER)
            return LiteralNode(float(token.value))
        if token.type == TokenType.IDENTIFIER:
            if token.value == "true":
                self.eat(TokenType.IDENTIFIER)
                ident_node = LiteralNode("1.0")
            elif token.value == "false":
                self.eat(TokenType.IDENTIFIER)
                ident_node = LiteralNode("0.0")
            else:
                ident_node = VariableNode(self.eat(TokenType.IDENTIFIER).value)
            while self.current_token().type == TokenType.ACCESS_OPERATOR:
                self.eat(TokenType.ACCESS_OPERATOR)
                right = self.eat(TokenType.IDENTIFIER)
                ident_node = MemberAccessNode(ident_node, right.value)
            if self.current_token().type == TokenType.OPENING_BRACKET:
                self.eat(TokenType.OPENING_BRACKET)
                arguments = []
                while self.current_token().type != TokenType.CLOSING_BRACKET and self.current_token().type != TokenType.EOF:
                    arguments.append(self.parse_expression())
                    if self.current_token().type == TokenType.COMMA:
                        self.eat(TokenType.COMMA)
                if self.current_token().type == TokenType.CLOSING_BRACKET:
                    self.eat(TokenType.CLOSING_BRACKET)
                else:
                    raise SyntaxError("Missing closing bracket for function call")
                return FunctionCallNode(ident_node, arguments)
            return ident_node
        if token.type == TokenType.OPENING_BRACKET:
            self.eat(TokenType.OPENING_BRACKET)
            node = self.parse_expression()
            self.eat(TokenType.CLOSING_BRACKET)
            return node
        raise SyntaxError(f"Unexpected token: {token}")
