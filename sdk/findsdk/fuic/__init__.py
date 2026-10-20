"""
fuic — the FinnUI compiler.

    from findsdk.fuic import compile_source, compile_file

The parts:

    lexer     the tokens
    parser    the tree
    theme     the token table, and the resolution of token references
    fbin      the binary format, read and written
    compiler  parse, resolve, write
    cli       the command line
"""

from .compiler import CompileError, compile_file, compile_source, dump
from .fbin import read as read_fbin, write as write_fbin
from .lexer import LexError, tokenize
from .parser import ParseError, parse
from .theme import Theme

__all__ = [
    "CompileError",
    "LexError",
    "ParseError",
    "Theme",
    "compile_file",
    "compile_source",
    "dump",
    "parse",
    "read_fbin",
    "tokenize",
    "write_fbin",
]
