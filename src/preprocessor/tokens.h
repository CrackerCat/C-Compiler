// Using x-macro.

X(T_NONE, "Invalid token")
    
// Preprocessing tokens.
X(PP_STRING, "String")
X(PP_CHARACTER_CONSTANT, "Character constant")
X(PP_HEADER_NAME, "Header name")
X(PP_IDENT, "Preprocessor Identifier")
X(PP_NUMBER, "Numerical value")
X(PP_PUNCT, "Punctiaion")
X(PP_HASH, "#")
X(PP_HHASH, "##")
X(PP_LPAR, "(")
X(PP_COMMA, ",")
X(PP_RPAR, ")")
X(PP_DIRECTIVE, "#")

// Conditional tokens.
X(COND_IF, "Conditional")
X(COND_IF_TRUE, "Conditional")
X(COND_IF_FALSE, "Conditional")
X(COND_ENDIF, "Conditional")

// Post-preprocessing tokens.
X(T_IDENT, "Identifier")
X(T_STRING, "String")
X(T_CHARACTER_CONSTANT, "String")
X(T_CHAR, "Character")
X(T_NUM, "Numerical")
X(T_TYPEDEF_NAME, "typedef-name")
    
// All the keywords.
KEY(T_KAUTO, "auto")
KEY(T_KCONST, "const")
KEY(T_KDOUBLE, "double")
KEY(T_KFLOAT, "float")
KEY(T_KINT, "int")
KEY(T_KSHORT, "short")
KEY(T_KBOOL, "_Bool")
KEY(T_KCOMPLEX, "_Complex")
KEY(T_KSTRUCT, "struct")
KEY(T_KUNSIGNED, "unsigned")
KEY(T_KBREAK, "break")
KEY(T_KCONTINUE, "continue")
KEY(T_KELSE, "else")
KEY(T_KFOR, "for")
KEY(T_KLONG, "long")
KEY(T_KSIGNED, "signed")
KEY(T_KSWITCH, "switch")
KEY(T_KVOID, "void")
KEY(T_KCASE, "case")
KEY(T_KDEFAULT, "default")
KEY(T_KENUM, "enum")
KEY(T_KGOTO, "goto")
KEY(T_KREGISTER, "register")
KEY(T_KSIZEOF, "sizeof")
KEY(T_KTYPEDEF, "typedef")
KEY(T_KVOLATILE, "volatile")
KEY(T_KCHAR, "char")
KEY(T_KDO, "do")
KEY(T_KEXTERN, "extern")
KEY(T_KIF, "if")
KEY(T_KRETURN, "return")
KEY(T_KSTATIC, "static")
KEY(T_KTHREAD_LOCAL, "_Thread_local")
KEY(T_KATOMIC, "_Atomic")
KEY(T_KRESTRICT, "restrict")
KEY(T_KUNION, "union")
KEY(T_KWHILE, "while")
KEY(T_KNORETURN, "_Noreturn")
KEY(T_KINLINE, "inline")
KEY(T_KALIGNAS, "_Alignas")
KEY(T_KALIGNOF, "_Alignof")
KEY(T_KSTATIC_ASSERT, "_Static_assert")
KEY(T_KGENERIC, "_Generic")

// builtins
KEY(T_KVA_START, "__builtin_va_start")
KEY(T_KVA_END, "__builtin_va_end")
KEY(T_KVA_ARG, "__builtin_va_arg")
KEY(T_KVA_COPY, "__builtin_va_copy")

// Punctuation with 3 characters.
// Tokenization of punctuation is dependent on them beeing in this order.
SYM(T_LSHIFTA, "<<=")
SYM(T_RSHIFTA, ">>=")
SYM(T_ELLIPSIS, "...")

// Punctuation with 2 characters.
SYM(T_GEQ, ">=")
SYM(T_LEQ, "<=")
SYM(T_EQ, "==")
SYM(T_NEQ, "!=")
SYM(T_BANDA, "&=")
SYM(T_XORA, "^=")
SYM(T_BORA, "|=")
SYM(T_ADDA, "+=")
SYM(T_SUBA, "-=")
SYM(T_MULA, "*=")
SYM(T_DIVA, "/=")
SYM(T_MODA, "%=")
SYM(T_LSHIFT, "<<")
SYM(T_RSHIFT, ">>")
SYM(T_AND, "&&")
SYM(T_OR, "||")
SYM(T_INC, "++")
SYM(T_DEC, "--")
SYM(T_ARROW, "->")

// Punctuation with 1 character.
SYM(T_ADD, "+")
SYM(T_SUB, "-")
SYM(T_DIV, "/")
SYM(T_MOD, "%")
SYM(T_G, ">")
SYM(T_L, "<")
SYM(T_NOT, "!")
SYM(T_BOR, "|")
SYM(T_XOR, "^")
SYM(T_BNOT, "~")
SYM(T_A, "=")
SYM(T_STAR, "*")
SYM(T_DOT, ".")
SYM(T_AMP, "&")
SYM(T_QUEST, "?")
SYM(T_COLON, ":")
SYM(T_SEMI_COLON, ";")
SYM(T_LPAR, "(")
SYM(T_RPAR, ")")
SYM(T_LBRACK, "[")
SYM(T_RBRACK, "]")
SYM(T_COMMA, ",")
SYM(T_LBRACE, "{")
SYM(T_RBRACE, "}")

// End of input.


X(T_EOI, "END OF INPUT")
