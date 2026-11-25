#!/usr/bin/env python3
"""as3cpp — traductor AS3 (salida de JPEXS) -> C++17.

Uso: python as3cpp.py <salida_dir> <archivo.as> [...]
Por clase genera <Clase>.h (declaración) y <Clase>.cpp (cuerpos); además
classes.h (declaraciones adelantadas), all.h (todas las cabeceras),
embeds.txt (recursos [Embed]) y stubs.txt (clases de flash.* que el runtime
debe dar).

Modelo de objetos:
- Toda clase deriva de as3::Object (o de su base). El constructor C++ solo pone
  valores por defecto; el constructor AS3 es `__construct(...)`.
  `new X(a)` -> `as3::make<X>(a)`; `super(a)` -> `Base::__construct(a)`.
- Los inicializadores de campos de instancia se ejecutan al principio de
  `__construct` (antes del cuerpo), como en AS3.
- Los estáticos se definen en el .cpp; el código suelto a nivel de clase
  (inicializador estático) va a `__static_init()`.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from flashtypes import parse as parse_flash  # noqa: E402

# ======================================================================= léxico
PUNCT = sorted("""
>>>= === !== >>> <<= >>= ... += -= *= /= %= &= |= ^= &&= ||= && || == != <= >= ++ -- << >> ::
{ } ( ) [ ] ; , . : ? = < > + - * / % ! ~ & | ^ @
""".split(), key=len, reverse=True)
KEYWORDS = {"package", "import", "class", "interface", "extends", "implements", "public",
            "private", "protected", "internal", "static", "final", "override", "dynamic",
            "native", "var", "const", "function", "return", "if", "else", "namespace", "use",
            "while", "for", "do", "break", "continue", "new", "this", "super",
            "null", "true", "false", "undefined", "is", "as", "in", "typeof", "delete", "switch",
            "case", "default", "throw", "try", "catch", "finally", "void", "instanceof"}
# palabras que pueden ser nombres según el contexto
SOFT = {"get", "set", "each", "dynamic", "native", "namespace", "use", "default", "override", "final"}


class Tok:
    __slots__ = ("k", "v", "line", "extra")

    def __init__(self, k, v, line, extra=None):
        self.k, self.v, self.line, self.extra = k, v, line, extra

    def __repr__(self):
        return f"{self.k}:{self.v!r}@{self.line}"


VALUE_END = {")", "]", "}"}


def lex(src):
    toks, i, line, n = [], 0, 1, len(src)

    def prev_is_value():
        if not toks:
            return False
        t = toks[-1]
        if t.k in ("num", "str", "id", "re"):
            return True
        if t.k == "kw" and t.v in ("this", "super", "null", "true", "false", "undefined"):
            return True
        return t.k == "p" and t.v in (")", "]")

    while i < n:
        c = src[i]
        if c == "\n":
            line += 1; i += 1; continue
        if c.isspace():
            i += 1; continue
        if src.startswith("//", i):
            j = src.find("\n", i); i = n if j < 0 else j; continue
        if src.startswith("/*", i):
            j = src.find("*/", i + 2); line += src.count("\n", i, j); i = j + 2; continue
        if c in "\"'":
            j, buf = i + 1, []
            while src[j] != c:
                if src[j] == "\\":
                    buf.append(src[j:j + 2]); j += 2
                else:
                    buf.append(src[j]); j += 1
            toks.append(Tok("str", "".join(buf), line)); i = j + 1; continue
        if c == "/" and not prev_is_value():  # literal de expresión regular
            j, incls = i + 1, False
            while True:
                ch = src[j]
                if ch == "\\":
                    j += 2; continue
                if ch == "[":
                    incls = True
                elif ch == "]":
                    incls = False
                elif ch == "/" and not incls:
                    break
                elif ch == "\n":
                    raise SyntaxError(f"línea {line}: regex sin cerrar")
                j += 1
            body = src[i + 1:j]
            m = re.match(r"[a-z]*", src[j + 1:])
            toks.append(Tok("re", body, line, m.group())); i = j + 1 + len(m.group()); continue
        m = re.match(r"0[xX][0-9a-fA-F]+|\d+\.\d*(?:[eE][-+]?\d+)?|\.\d+(?:[eE][-+]?\d+)?|\d+(?:[eE][-+]?\d+)?", src[i:])
        if m and (c.isdigit() or (c == "." and i + 1 < n and src[i + 1].isdigit())):
            toks.append(Tok("num", m.group(), line)); i += len(m.group()); continue
        if c == "§":  # nombres ofuscados que JPEXS escribe entre §...§
            j = src.index("§", i + 1)
            toks.append(Tok("id", src[i:j + 1], line)); i = j + 1; continue
        m = re.match(r"[A-Za-z_$][A-Za-z0-9_$]*", src[i:])
        if m:
            v = m.group()
            toks.append(Tok("kw" if v in KEYWORDS else "id", v, line)); i += len(v); continue
        for p in PUNCT:
            if src.startswith(p, i):
                toks.append(Tok("p", p, line)); i += len(p); break
        else:
            raise SyntaxError(f"línea {line}: carácter inesperado {c!r}")
    toks.append(Tok("eof", "", line))
    return toks


# ======================================================================= parser
class Unsupported(Exception):
    pass


class Parser:
    MODS = {"public", "private", "protected", "internal", "static", "final", "override", "dynamic", "native"}

    def __init__(self, toks, fname, rename=(), decl=()):
        # La clase del juego se renombra siempre en su declaracion (y su constructor); los usos solo si el archivo
        # no importa la clase homonima de flash.* (en ese caso `Mouse` dentro del archivo es la de Flash).
        declared = None
        for i, t in enumerate(toks):
            if t.k == "kw" and t.v in ("class", "interface") and i + 1 < len(toks) and toks[i + 1].v in decl:
                declared = toks[i + 1].v
                toks[i + 1].v = declared + "_g"
            elif declared and t.k == "kw" and t.v == "function" and i + 2 < len(toks) and toks[i + 1].v == declared                     and toks[i + 2].v == "(":
                toks[i + 1].v = declared + "_g"
            elif t.k == "id" and t.v in rename:
                t.v = t.v + "_g"
        self.t, self.i, self.fname = toks, 0, fname
        self.ns = set()  # namespaces declarados/usados (mx_internal...)

    # ---- utilidades
    def peek(self, o=0):
        return self.t[min(self.i + o, len(self.t) - 1)]

    def at(self, v, o=0):
        t = self.peek(o)
        return t.v == v and t.k in ("p", "kw", "id")

    def eat(self, v=None):
        t = self.t[self.i]
        if v is not None and not (t.v == v and t.k in ("p", "kw", "id")):
            self.err(f"se esperaba {v!r}, hay {t.v!r}")
        self.i += 1
        return t

    def accept(self, v):
        if self.at(v):
            self.i += 1
            return True
        return False

    def err(self, msg):
        raise SyntaxError(f"{self.fname}:{self.peek().line}: {msg}")

    def is_name(self, o=0):
        t = self.peek(o)
        return t.k == "id" or (t.k == "kw" and t.v in SOFT)

    def ident(self):
        if self.is_name():
            return self.eat().v
        # nombres de miembros pueden ser palabras clave tras '.'
        t = self.peek()
        if t.k == "kw":
            self.i += 1
            return t.v
        self.err(f"se esperaba un nombre, hay {t.v!r}")

    def qname(self):
        parts = [self.ident()]
        while self.at("."):
            if self.at("*", 1):
                self.eat("."); self.eat("*"); parts.append("*"); break
            if self.peek(1).k in ("id", "kw") and not self.at("<", 1):
                self.eat("."); parts.append(self.ident())
            else:
                break
        return ".".join(parts)

    def type_(self):
        if self.accept("*"):
            return "*"
        if self.accept("void"):
            return "void"
        name = self.qname()
        if name.split(".")[-1] == "Vector" and self.at(".") and self.at("<", 1):
            self.eat("."); self.eat("<"); inner = self.type_(); self.close_angle()
            return f"Vector.<{inner}>"
        return name

    def close_angle(self):
        # '>>' cierra dos Vector.<Vector.<T>>
        t = self.peek()
        if t.v == ">":
            self.i += 1
        elif t.v == ">>":
            t.v = ">"  # consume uno
        elif t.v == ">>>":
            t.v = ">>"
        else:
            self.err("se esperaba '>'")

    def metadata(self):
        """[Embed(source=..., mimeType=...)] y similares. Devuelve dict o None."""
        if not self.at("["):
            return None
        start = self.i
        self.eat("[")
        if not self.is_name():
            self.i = start
            return None
        name = self.ident()
        attrs = {}
        if self.accept("("):
            while not self.at(")"):
                if self.is_name() and self.at("=", 1):
                    k = self.ident(); self.eat("=")
                    attrs[k] = self.eat().v
                else:
                    attrs.setdefault("_", []).append(self.eat().v)
                self.accept(",")
            self.eat(")")
        self.eat("]")
        return {"name": name, **attrs}

    # ---- archivo
    def file(self):
        self.eat("package")
        pkg = ""
        if not self.at("{"):
            pkg = self.qname()
        self.eat("{")
        imports, classes = [], []
        meta = []
        while not self.at("}"):
            if self.accept("import"):
                imports.append(self.qname()); self.accept(";"); continue
            md = self.metadata()
            if md:
                meta.append(md); continue
            mods = self.mods()
            if self.accept("namespace"):
                self.ident()
                if self.accept("="):
                    self.eat()
                self.accept(";"); continue
            if self.at("use"):
                self.eat(); self.eat("namespace"); self.ident(); self.accept(";"); continue
            if self.at("function"):
                raise Unsupported("función de paquete (fuera de clase)")
            if self.at("var") or self.at("const"):
                raise Unsupported("variable de paquete (fuera de clase)")
            cl = self.class_(mods, meta)
            cl["imports"] = imports
            cl["pkg"] = pkg
            classes.append(cl)
            meta = []
        self.eat("}")
        return imports, classes

    def mods(self):
        m = set()
        while True:
            t = self.peek()
            if t.k == "kw" and t.v in self.MODS and not (t.v == "dynamic" and self.at("(", 1)):
                m.add(self.eat().v); continue
            # calificador de namespace usado como modificador: `mx_internal var x`
            if t.k == "id" and self.peek(1).k == "kw" and self.peek(1).v in (
                    "var", "const", "function", "static", "public", "private", "protected",
                    "override", "final", "internal") and t.v not in ("get", "set"):
                self.ns.add(t.v); self.eat(); m.add("ns:" + t.v); continue
            return m

    def class_(self, mods, meta):
        is_iface = False
        if self.accept("interface"):
            is_iface = True
        else:
            self.eat("class")
        name = self.ident()
        base, ifaces = None, []
        if self.accept("extends"):
            base = self.qname()
            while self.accept(","):  # interfaces pueden extender varias
                ifaces.append(self.qname())
        if self.accept("implements"):
            ifaces.append(self.qname())
            while self.accept(","):
                ifaces.append(self.qname())
        self.eat("{")
        members, sinit = [], []
        while not self.at("}"):
            if self.metadata():
                continue
            save = self.i
            mods = self.mods()
            if self.at("var") or self.at("const"):
                kind = self.eat().v
                while True:
                    n = self.ident(); ty = "*"
                    if self.accept(":"):
                        ty = self.type_()
                    init = self.assign() if self.accept("=") else None
                    members.append(("field", n, ty, init, mods, kind == "const"))
                    if not self.accept(","):
                        break
                self.accept(";")
            elif self.accept("function"):
                acc = None
                if self.peek().v in ("get", "set") and self.is_name(1):
                    acc = self.eat().v
                n = self.ident()
                params, rest = self.params()
                ret = self.type_() if self.accept(":") else "*"
                body = None if is_iface or "native" in mods else self.block()
                if body is None:
                    self.accept(";")
                members.append(("method", n, params, ret, body, mods, acc, rest))
            elif self.at("use"):
                self.eat(); self.eat("namespace"); self.ident(); self.accept(";")
            else:
                # código suelto en la clase = inicializador estático
                self.i = save
                sinit.append(self.stmt())
        self.eat("}")
        embed = next((m for m in meta if m["name"] == "Embed"), None)
        return {"name": name, "base": base, "ifaces": ifaces, "members": members,
                "sinit": sinit, "iface": is_iface, "embed": embed, "file": self.fname}

    def params(self):
        self.eat("(")
        ps, rest = [], None
        while not self.at(")"):
            if self.accept("..."):
                rest = self.ident()
                if self.accept(":"):
                    self.type_()
                self.accept(","); continue
            n = self.ident(); ty = "*"
            if self.accept(":"):
                ty = self.type_()
            d = self.assign() if self.accept("=") else None
            ps.append((n, ty, d))
            self.accept(",")
        self.eat(")")
        return ps, rest

    # ---- sentencias
    def block(self):
        self.eat("{")
        out = []
        while not self.at("}"):
            out.append(self.stmt())
        self.eat("}")
        return ("block", out)

    def stmt(self):
        if self.at("{"):
            return self.block()
        if self.accept(";"):
            return ("empty",)
        # etiqueta:  nombre ':' sentencia
        if self.is_name() and self.at(":", 1) and not self.at("::", 1):
            lab = self.ident(); self.eat(":")
            return ("label", lab, self.stmt())
        if self.at("var") or self.at("const"):
            s = self.vardecl(); self.accept(";"); return s
        if self.at("function") and self.is_name(1):
            self.eat("function")
            n = self.ident()
            params, rest = self.params()
            ret = self.type_() if self.accept(":") else "*"
            return ("localfn", n, params, ret, self.block(), rest)
        if self.accept("return"):
            e = None if self.at(";") or self.at("}") else self.expr()
            self.accept(";"); return ("return", e)
        if self.accept("if"):
            self.eat("("); c = self.expr(); self.eat(")")
            th = self.stmt(); el = self.stmt() if self.accept("else") else None
            return ("if", c, th, el)
        if self.accept("while"):
            self.eat("("); c = self.expr(); self.eat(")")
            return ("while", c, self.stmt())
        if self.accept("do"):
            body = self.stmt(); self.eat("while"); self.eat("("); c = self.expr(); self.eat(")")
            self.accept(";")
            return ("dowhile", body, c)
        if self.accept("for"):
            each = False
            if self.at("each"):
                self.eat(); each = True
            self.eat("(")
            # for (var x in obj) / for each (var x in obj)
            save = self.i
            if self.at("var") or self.at("const"):
                self.eat()
                n = self.ident(); ty = "*"
                if self.accept(":"):
                    ty = self.type_()
                if self.accept("in"):
                    obj = self.expr(); self.eat(")")
                    return ("forin", each, n, ty, True, obj, self.stmt())
                self.i = save
            elif self.is_name() and self.at("in", 1):
                n = self.ident(); self.eat("in")
                obj = self.expr(); self.eat(")")
                return ("forin", each, n, None, False, obj, self.stmt())
            if each:
                self.err("for each con forma desconocida")
            init = None if self.at(";") else (self.vardecl() if (self.at("var") or self.at("const")) else ("expr", self.expr()))
            self.eat(";")
            c = None if self.at(";") else self.expr(); self.eat(";")
            st = None if self.at(")") else self.expr(); self.eat(")")
            return ("for", init, c, st, self.stmt())
        if self.accept("switch"):
            self.eat("("); v = self.expr(); self.eat(")"); self.eat("{")
            cases = []
            while not self.at("}"):
                if self.accept("case"):
                    e = self.expr(); self.eat(":")
                else:
                    self.eat("default"); self.eat(":"); e = None
                body = []
                while not (self.at("case") or self.at("default") and self.at(":", 1) or self.at("}")):
                    body.append(self.stmt())
                cases.append((e, body))
            self.eat("}")
            return ("switch", v, cases)
        if self.accept("break"):
            lab = self.ident() if self.is_name() and self.peek().line == self.t[self.i - 1].line else None
            self.accept(";"); return ("break", lab)
        if self.accept("continue"):
            lab = self.ident() if self.is_name() and self.peek().line == self.t[self.i - 1].line else None
            self.accept(";"); return ("continue", lab)
        if self.accept("throw"):
            e = self.expr(); self.accept(";"); return ("throw", e)
        if self.accept("try"):
            body = self.block(); catches = []; fin = None
            while self.accept("catch"):
                self.eat("("); n = self.ident(); ty = "*"
                if self.accept(":"):
                    ty = self.type_()
                self.eat(")"); catches.append((n, ty, self.block()))
            if self.accept("finally"):
                fin = self.block()
            return ("try", body, catches, fin)
        if self.at("use"):
            self.eat(); self.eat("namespace"); self.ident(); self.accept(";"); return ("empty",)
        e = self.expr(); self.accept(";")
        return ("expr", e)

    def vardecl(self):
        self.eat()  # var/const
        decls = []
        while True:
            n = self.ident(); ty = "*"
            if self.accept(":"):
                ty = self.type_()
            init = self.assign() if self.accept("=") else None
            decls.append((n, ty, init))
            if not self.accept(","):
                break
        return ("var", decls)

    # ---- expresiones (ascenso por precedencia)
    BIN = [["||"], ["&&"], ["|"], ["^"], ["&"], ["==", "!=", "===", "!=="],
           ["<", ">", "<=", ">=", "is", "as", "in", "instanceof"], ["<<", ">>", ">>>"], ["+", "-"], ["*", "/", "%"]]
    ASSIGN = {"=", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>=", ">>>=", "&&=", "||="}

    def expr(self):
        e = self.assign()
        while self.at(","):
            self.eat(",")
            e = ("comma", e, self.assign())
        return e

    def assign(self):
        lhs = self.ternary()
        t = self.peek()
        if t.k == "p" and t.v in self.ASSIGN:
            self.eat()
            return ("assign", t.v, lhs, self.assign())
        return lhs

    def ternary(self):
        c = self.binary(0)
        if self.accept("?"):
            a = self.assign(); self.eat(":"); b = self.assign()
            return ("cond", c, a, b)
        return c

    def binary(self, lvl):
        if lvl == len(self.BIN):
            return self.unary()
        e = self.binary(lvl + 1)
        while self.peek().v in self.BIN[lvl] and self.peek().k in ("p", "kw"):
            op = self.eat().v
            if op in ("is", "as"):
                e = ("bin", op, e, ("type", self.type_()))
            else:
                e = ("bin", op, e, self.binary(lvl + 1))
        return e

    def unary(self):
        t = self.peek()
        if t.k == "p" and t.v in ("!", "-", "+", "~"):
            self.eat(); return ("un", t.v, self.unary())
        if t.k == "p" and t.v in ("++", "--"):
            self.eat(); return ("pre", t.v, self.unary())
        if self.accept("typeof"):
            return ("typeof", self.unary())
        if self.accept("delete"):
            return ("delete", self.unary())
        if self.accept("void"):
            return ("comma", self.unary(), ("undef",))
        return self.postfix()

    def postfix(self):
        e = self.primary()
        if e[0] == "id" and e[1] in ("flash", "mx", "org", "com") and self.at(".") and self.peek(1).k in ("id", "kw"):
            # ruta de paquete completa: se queda con el ultimo segmento (la clase o la funcion)
            name = e[1]
            while self.at(".") and self.peek(1).k in ("id", "kw") and (name in ("flash", "mx", "org", "com") or name[:1].islower()):
                self.eat(".")
                name = self.ident()
                if name[:1].isupper() or not self.at("."):
                    break
            e = ("id", name)
        while True:
            if self.at(".") and self.at("<", 1):  # Vector.<T>(...) o new Vector.<T>
                self.eat("."); self.eat("<"); inner = self.type_(); self.close_angle()
                e = ("vectype", inner)
            elif self.accept("."):
                if self.accept("@"):
                    e = ("xmlattr", e, self.ident()); continue
                if self.at("("):
                    raise Unsupported("E4X (filtro .())")
                e = ("member", e, self.ident())
            elif self.at("::"):
                self.eat("::")
                e = ("member", e if e[0] != "id" else ("nsq", e[1]), self.ident())
                e = ("member", e[1][1] and ("this",), e[2]) if e[1][0] == "nsq" else e
            elif self.accept("["):
                i = self.expr(); self.eat("]"); e = ("index", e, i)
            elif self.at("("):
                e = ("call", e, self.args())
            elif self.peek().v in ("++", "--") and self.peek().k == "p" and \
                    self.peek().line == self.t[self.i - 1].line:
                e = ("post", self.eat().v, e)
            else:
                return e

    def args(self):
        self.eat("(")
        a = []
        while not self.at(")"):
            a.append(self.assign()); self.accept(",")
        self.eat(")")
        return a

    def primary(self):
        t = self.eat()
        if t.k == "num":
            return ("num", t.v)
        if t.k == "str":
            return ("str", t.v)
        if t.k == "re":
            return ("regex", t.v, t.extra)
        if t.k == "kw":
            if t.v in ("true", "false"):
                return ("bool", t.v)
            if t.v == "null":
                return ("null",)
            if t.v == "undefined":
                return ("undef",)
            if t.v == "this":
                return ("this",)
            if t.v == "super":
                return ("super",)
            if t.v == "new":
                if self.at("<"):  # new <T>[a, b]
                    self.eat("<"); inner = self.type_(); self.close_angle()
                    return ("vecliteral", inner, self.primary()[1])
                if self.at("("):
                    callee = self.primary()
                    return ("newexpr", callee, self.args() if self.at("(") else [])
                ty = self.type_()
                a = self.args() if self.at("(") else []
                return ("new", ty, a)
            if t.v == "function":
                n = self.ident() if self.is_name() else None
                params, rest = self.params()
                ret = self.type_() if self.accept(":") else "*"
                return ("lambda", params, ret, self.block(), rest)
            if t.v in SOFT:
                return ("id", t.v)
        if t.k == "id":
            return ("id", t.v)
        if t.v == "(":
            e = self.expr(); self.eat(")"); return ("paren", e)
        if t.v == "[":
            items = []
            while not self.at("]"):
                items.append(self.assign()); self.accept(",")
            self.eat("]")
            return ("array", items)
        if t.v == "{":
            props = []
            while not self.at("}"):
                k = self.eat()
                if k.k not in ("id", "str", "num", "kw"):
                    self.err("clave de objeto inválida")
                self.eat(":")
                props.append((k.v, self.assign())); self.accept(",")
            self.eat("}")
            return ("object", props)
        if t.v == "<":
            raise Unsupported("literal XML (E4X)")
        self.i -= 1
        self.err(f"expresión inesperada {t.v!r}")


# ======================================================================= emisión C++
PRIM = {"Number": "double", "int": "int32_t", "uint": "uint32_t", "Boolean": "bool",
        "String": "as3::String", "void": "void", "*": "as3::Any", "Object": "as3::Any"}
NUMERIC = {"Number", "int", "uint"}
# clases que da el runtime (namespace as3) — el resto de flash.*/mx.* va a flash::
AS3_BUILTIN = {"Array", "Dictionary", "Function", "Class", "RegExp", "XML", "XMLList", "Date",
               "Error", "Math", "JSON", "ByteArray"}
GLOBAL_FNS = {"trace", "int", "uint", "Number", "String", "Boolean", "isNaN", "isFinite",
              "parseInt", "parseFloat", "getTimer", "getQualifiedClassName", "getDefinitionByName",
              "navigateToURL", "setTimeout", "clearTimeout", "encodeURI", "escape", "unescape",
              "describeType", "Array", "Object", "XML", "Vector", "fscommand", "escape", "setInterval", "clearInterval"}
CPP_RESERVED = {"auto", "char", "default", "delete", "double", "float", "long", "short", "signed",
                "unsigned", "register", "struct", "union", "template", "typename", "operator",
                "friend", "virtual", "inline", "volatile", "mutable", "explicit", "sizeof",
                "typedef", "enum", "extern", "goto", "asm", "new", "this", "bool", "int", "and",
                "or", "not", "xor", "union", "signed", "small", "near", "far", "min", "max", "errno",
                "stdin", "stdout", "stderr", "EOF", "NULL", "TRUE", "FALSE", "DELETE", "IN", "OUT",
                "ERROR", "INFINITE", "Rectangle", "Polygon", "Ellipse", "LoadImage", "PlaySound",
                "CreateWindow", "DrawText", "GetObject", "SendMessage", "near", "far", "interface",
                "linux", "unix", "major", "minor", "assert", "clock", "rand"}


# clases integradas con miembros estáticos que viven en el runtime (as3rt.h)
STATIC_HOLDERS = {"Number": "as3::Number_", "int": "as3::int_", "uint": "as3::uint_", "String": "as3::String_",
                  "Object": "as3::Object_"}


def mangle(n):
    if n.startswith("§"):
        return "obf_" + "".join(ch if ch.isalnum() else {"_": "_", "-": "m"}.get(ch, f"x{ord(ch):x}")
                                for ch in n[1:-1])
    if n in CPP_RESERVED:
        return n + "_"
    return n.replace("$", "_S_")


def short(t):
    if not isinstance(t, str):
        return None
    return t.split(".")[-1] if t and not t.startswith("Vector.<") else t


class ClassInfo:
    def __init__(self, c):
        self.c = c
        self.name = c["name"]
        self.flash = False
        self.iface = c["iface"]
        self.base = short(c["base"]) if c["base"] else None
        self.ifaces = [short(i) for i in c["ifaces"]]
        if self.iface and self.base:  # interface A extends B, C: todo son interfaces
            self.ifaces = [self.base] + self.ifaces
            self.base = None
        self.fields, self.sfields, self.methods, self.smethods = {}, {}, {}, {}
        self.getters, self.setters, self.consts = {}, {}, set()
        for m in c["members"]:
            if m[0] == "field":
                (self.sfields if "static" in m[4] else self.fields)[m[1]] = m[2]
            elif m[6] == "get":
                self.getters[m[1]] = (m[3], "static" in m[5])
            elif m[6] == "set":
                self.setters[m[1]] = (m[2][0][1] if m[2] else "*", "static" in m[5])
            elif m[1] != self.name:
                (self.smethods if "static" in m[5] else self.methods)[m[1]] = (m[3], m[2])
        inst = set(self.fields) | set(self.methods)
        stat = set(self.sfields) | set(self.smethods)
        self.conflict = inst & stat  # AS3 permite estático e instancia con el mismo nombre; C++ no


class FlashInfo:
    """Clase de flash.* descrita por runtime/flash.h (solo para tipar; no se traduce)."""

    def __init__(self, name, d):
        self.name = name
        self.flash = True
        self.iface = False
        self.ifaces = []
        self.base = d["base"] if d["base"] not in (None, "Object") else None
        self.fields, self.sfields = dict(d["fields"]), dict(d["sfields"])
        self.methods = {k: (v[0], [tuple(p) for p in v[1]]) for k, v in d["methods"].items()}
        self.smethods = {k: (v[0], [tuple(p) for p in v[1]]) for k, v in d["smethods"].items()}
        self.getters = {k: tuple(v) for k, v in d["getters"].items()}
        self.setters = {k: tuple(v) for k, v in d["setters"].items()}
        self.conflict = set()
        self.c = {"members": [], "name": name}


class Ctx:
    """Contexto de emisión de un método."""

    def __init__(self, scope, static, rest=None):
        self.scope = scope  # nombre -> tipo AS3
        self.static = static
        self.labels = []
        self.tmp = 0


class Emitter:
    def __init__(self, infos, flashinfo=None):
        self.translated = infos
        self.static_deps = {}  # clase -> clases cuyos estaticos deben iniciarse antes
        self.flashinfo = flashinfo or {}
        self.infos = self.make_view([])
        self.stubs = set()

    def make_view(self, imports, own=None):
        """Nombres visibles desde un archivo: clases del juego + flash.*; un `import flash.x.Name`
        explicito gana a una clase del juego con el mismo nombre (p. ej. flash.ui.Mouse)."""
        view = dict(self.flashinfo)
        view.update(self.translated)
        for imp in imports:
            parts = imp.split(".")
            if parts[0] in ("flash", "mx") and parts[-1] in self.flashinfo:
                view[parts[-1]] = self.flashinfo[parts[-1]]
        if own and own in self.translated:
            view[own] = self.translated[own]
        return view

    def cn(self, cls):
        """Nombre C++ de una clase (con `flash::` si es de Flash)."""
        ci = self.infos.get(cls)
        return ("flash::" + cls) if ci is not None and ci.flash else mangle(cls)

    def sn(self, cls, n):
        """Nombre C++ de un miembro ESTÁTICO de `cls` (prefijo s_ si choca con uno de instancia)."""
        ci = self.infos.get(cls)
        return ("s_" + mangle(n)) if ci is not None and n in ci.conflict else mangle(n)

    # ---------- tipos
    def ctype(self, t):
        if t is None:
            return "as3::Any"
        if t.startswith("Vector.<"):
            return f"as3::Vector<{self.ctype(t[8:-1])}>*"
        s = short(t)
        if s in PRIM:
            return PRIM[s]
        if s == "Function":
            return "as3::Function"
        if s in AS3_BUILTIN:
            return f"as3::{s}*"
        if s in self.infos:
            return self.cn(s) + "*"
        self.stubs.add(s)
        return f"flash::{mangle(s)}*"

    def cclass(self, t):
        """Nombre de la clase C++ (sin puntero) para new/is/as/estáticos."""
        c = self.ctype(t)
        return c[:-1] if c.endswith("*") else c

    def default(self, t):
        s = short(t)
        return {"Number": "as3::NaN", "int": "0", "uint": "0u", "Boolean": "false"}.get(s, "{}")

    def is_ref(self, t):
        """True si el tipo AS3 se representa como puntero (acceso con ->)."""
        if t is None or not isinstance(t, str):
            return False
        s = short(t)
        return not (s in PRIM or s == "Function")

    def is_class_name(self, n):
        return n in self.infos or n in AS3_BUILTIN or n in ("Math", "Vector")

    # ---------- búsqueda de miembros por la jerarquía
    def find(self, cls, name, kind):
        seen = set()
        stack = [cls]
        while stack:
            cls = stack.pop(0)
            if cls in seen or cls not in self.infos:
                continue
            seen.add(cls)
            ci = self.infos[cls]
            d = getattr(ci, kind)
            if name in d:
                return cls, d[name]
            if ci.base:
                stack.append(ci.base)
            stack.extend(ci.ifaces)
        return None, None

    def member_type(self, owner, name, static=False):
        s = short(owner) if owner else None
        if s not in self.infos:
            return None
        for kind in (("sfields", "smethods") if static else ("fields", "methods")) + ("getters",):
            c, v = self.find(s, name, kind)
            if c:
                if kind in ("methods", "smethods"):
                    return ("Function", v[0])
                if kind == "getters":
                    return v[0]
                return v
        return None

    # tipo estático AS3 de una expresión (None = desconocido / dinámico)
    def stype(self, e, cx):
        k = e[0]
        if k == "tmp":
            return e[2]
        if k == "this":
            return self.cls
        if k == "paren":
            return self.stype(e[1], cx)
        if k == "id":
            n = e[1]
            if n in cx.scope:
                return cx.scope[n]
            t = self.member_type(self.cls, n) if not cx.static else None
            if t is None:
                t = self.member_type(self.cls, n, static=True)
            if isinstance(t, tuple):
                return "Function"
            if t is None and self.is_class_name(n):
                return ("class", n)
            return t
        if k == "member":
            ot = self.stype(e[1], cx)
            if isinstance(ot, tuple) and ot[0] == "class":
                t = self.member_type(ot[1], e[2], static=True)
            else:
                t = self.member_type(ot, e[2]) if ot else None
                if t is None and ot in ("String",) and e[2] == "length":
                    return "int"
                if t is None and ot and (ot == "Array" or str(ot).startswith("Vector.<")) and e[2] == "length":
                    return "uint"
            return "Function" if isinstance(t, tuple) else t
        if k == "call":
            f = e[1]
            if f[0] == "id" and f[1] in ("int", "uint", "Number", "String", "Boolean"):
                return f[1]
            if f[0] == "id" and (f[1] in self.infos or f[1] in AS3_BUILTIN):
                return f[1]  # conversión de tipo  Cls(x)
            if f[0] == "member":
                ot = self.stype(f[1], cx)
                if isinstance(ot, tuple) and ot[0] == "class":
                    if ot[1] == "Math":
                        return "Number"
                    t = self.member_type(ot[1], f[2], static=True)
                else:
                    t = self.member_type(ot, f[2]) if ot else None
                return t[1] if isinstance(t, tuple) else None
            if f[0] == "id":
                n = f[1]
                if n not in cx.scope:
                    t = self.member_type(self.cls, n) or self.member_type(self.cls, n, static=True)
                    return t[1] if isinstance(t, tuple) else None
            return None
        if k == "index":
            ot = self.stype(e[1], cx)
            if ot and str(ot).startswith("Vector.<"):
                return ot[8:-1]
            return None
        if k == "new":
            return e[1] if (short(e[1]) in self.infos or short(e[1]) in AS3_BUILTIN) else None
        if k in ("num",):
            return "Number"
        if k == "str":
            return "String"
        if k == "bool":
            return "Boolean"
        if k == "array":
            return "Array"
        if k == "bin":
            op = e[1]
            if op == "as":
                return e[3][1]
            if op in ("==", "!=", "===", "!==", "<", ">", "<=", ">=", "is", "in", "instanceof", "&&", "||"):
                if op not in ("&&", "||"):
                    return "Boolean"
                sa, sb = self.stype(e[2], cx), self.stype(e[3], cx)
                return sa if sa == sb else None
            a, b = self.stype(e[2], cx), self.stype(e[3], cx)
            if op == "+" and ("String" in (a, b)):
                return "String"
            if op in ("&", "|", "^", "<<", ">>"):
                return "int"
            if op == ">>>":
                return "uint"
            if a in NUMERIC and b in NUMERIC:
                return "Number"
            return None
        if k == "un":
            return "Boolean" if e[1] == "!" else self.stype(e[2], cx)
        if k in ("pre", "post"):
            return self.stype(e[2], cx)
        if k == "cond":
            ta, tb = self.stype(e[2], cx), self.stype(e[3], cx)
            if ta == tb:
                return ta
            if tb == "null" and ta and ta not in NUMERIC and ta != "Boolean":
                return ta
            if ta == "null" and tb and tb not in NUMERIC and tb != "Boolean":
                return tb
            return None
        if k == "assign":
            return self.stype(e[2], cx)
        if k == "null":
            return "null"
        if k == "undef":
            return "undef"
        if k == "regex":
            return "RegExp"
        if k == "lambda":
            return "Function"
        if k == "vectype":
            return ("class", "Vector.<" + e[1] + ">")
        return None

    # ---------- expresiones
    def lit_num(self, v):
        if v.lower().startswith("0x"):
            n = int(v, 16)
            return f"{n}u" if n > 0x7fffffff else str(n)
        if re.fullmatch(r"\d+", v):
            n = int(v)
            return str(n) if n <= 0x7fffffff else f"{float(n)!r}"
        return v if "." in v or "e" in v.lower() else v

    def str_lit(self, s):
        # las secuencias de escape de AS3 coinciden con las de C++ salvo \u
        s = re.sub(r"\\u([0-9a-fA-F]{4})", lambda m: chr(int(m.group(1), 16)), s)
        s = s.replace("?", "\\?")  # evitar trigrafos
        out = []
        for ch in s:
            if ord(ch) > 127:
                out.append("".join(f"\\x{b:02x}" for b in ch.encode("utf-8")) + '""')
            else:
                out.append(ch)
        return '"' + "".join(out) + '"'

    def conv(self, code, from_t, to_t):
        """Conversión implícita de AS3 al asignar/pasar/devolver."""
        if to_t is None:
            return code
        ts = short(to_t)
        fs = short(from_t) if isinstance(from_t, str) else None
        if fs == ts:
            return code
        if fs == "null":
            if ts == "Function":
                return "as3::Function()"
            return code
        if fs == "undef":
            return {"Number": "as3::NaN", "int": "0", "uint": "0u", "Boolean": "false", "String": "as3::String()",
                    "Function": "as3::Function()"}.get(ts, "as3::undefined" if ts in ("*", "Object") else "nullptr")
        if ts == "Function" and (fs is None or fs in ("*", "Object")):
            return f"as3::fromAny<as3::Function>({code})"
        if ts in ("int", "uint", "Number", "Boolean", "String"):
            if fs in NUMERIC and ts in NUMERIC:
                return {"int": f"as3::toInt({code})", "uint": f"as3::toUint({code})",
                        "Number": f"double({code})"}[ts]
            return {"int": f"as3::toInt({code})", "uint": f"as3::toUint({code})",
                    "Number": f"as3::toNumber({code})", "Boolean": f"as3::truthy({code})",
                    "String": f"as3::toString({code})"}[ts]
        if ts in ("*", "Object") or ts == "Function":
            return code
        if fs is None or fs in ("*", "Object"):
            return f"as3::cast<{self.cclass(to_t)}>({code})"
        return code

    def ex(self, e, cx, want=None):
        code = self._ex(e, cx)
        if want is not None:
            return self.conv(code, self.stype(e, cx), want)
        return code

    def args_for(self, params, args, cx):
        out = []
        for i, a in enumerate(args):
            pt = params[i][1] if params and i < len(params) else None
            out.append(self.ex(a, cx, pt))
        return out

    def _ex(self, e, cx):
        k = e[0]
        if k == "tmp":
            return e[1]
        if k == "num":
            return self.lit_num(e[1])
        if k == "str":
            return f"as3::String({self.str_lit(e[1])})"
        if k == "bool":
            return e[1]
        if k == "null":
            return "nullptr"
        if k == "undef":
            return "as3::undefined"
        if k == "this":
            return "this"
        if k == "paren":
            return f"({self._ex(e[1], cx)})"
        if k == "regex":
            return f'as3::regex(R"AS3RX({e[1]})AS3RX", "{e[2]}")'
        if k == "id":
            return self.ident(e[1], cx)
        if k == "member":
            return self.member(e, cx)
        if k == "index":
            return f"as3::at({self._ex(e[1], cx)}, {self._ex(e[2], cx)})"
        if k == "call":
            return self.call(e, cx)
        if k == "new":
            if short(e[1]) == "Object":
                return "as3::Any(as3::make<as3::Object>())"
            dotted = e[1].split(".")
            if (len(dotted) == 1 and (e[1] in cx.scope or (e[1] not in self.infos and e[1][:1] in "abcdefghijklmnopqrstuvwxyz_") or
                                      self.member_type(self.cls, e[1]) or self.member_type(self.cls, e[1], True))) or \
                    (len(dotted) > 1 and (dotted[0] == "this" or dotted[0] in cx.scope)):
                if dotted[0] == "this":
                    ex_ = ("this",)
                else:
                    ex_ = ("id", dotted[0])
                for p_ in dotted[1:]:
                    if p_ != "this":
                        ex_ = ("member", ex_, p_)
                return f"as3::newDynamic({self._ex(ex_, cx)}, {{{', '.join('as3::Any(' + self._ex(a, cx) + ')' for a in e[2])}}})"
            ci = self.infos.get(short(e[1]))
            params = None
            if ci:
                _, ctor = self.find_ctor(short(e[1]))
                params = ctor
            return f"as3::make<{self.cclass(e[1])}>({', '.join(self.args_for(params, e[2], cx))})"
        if k == "newexpr":
            return f"as3::newDynamic({self._ex(e[1], cx)}, {{{', '.join(self._ex(a, cx) for a in e[2])}}})"
        if k == "vecliteral":
            return f"as3::vectorOf<{self.ctype(e[1])}>({{{', '.join(self.ex(a, cx, e[1]) for a in e[2])}}})"
        if k == "vectype":
            return f"as3::VectorClass<{self.ctype(e[1])}>()"
        if k == "assign":
            return self.assign(e, cx)
        if k == "comma":
            return f"({self._ex(e[1], cx)}, {self._ex(e[2], cx)})"
        if k == "bin":
            return self.binop(e, cx)
        if k == "un":
            if e[1] == "!":
                return f"(!as3::truthy({self._ex(e[2], cx)}))"
            if e[1] == "~":
                return f"(~as3::toInt({self._ex(e[2], cx)}))"
            t = self.stype(e[2], cx)
            inner = self._ex(e[2], cx)
            if t == "uint" or (t == "int" and e[1] == "-"):
                # AS3: -uint y -int dan un Number (-2147483648 no desborda); en C++ la negacion de uint32_t daria la vuelta
                return f"({e[1]}double({inner}))"
            return f"({e[1]}{inner})" if t in NUMERIC else f"({e[1]}as3::toNumber({inner}))"
        if k in ("pre", "post"):
            tgt = e[2]
            if self.is_setter_target(tgt, cx):
                one = f"{'+' if e[1] == '++' else '-'} 1"
                return self.assign(("assign", "+=" if e[1] == "++" else "-=", tgt, ("num", "1")), cx)
            dlt = "1" if e[1] == "++" else "-1"
            post = "true" if k == "post" else "false"
            if tgt[0] == "index":
                return f"as3::incAt({self._ex(tgt[1], cx)}, {self._ex(tgt[2], cx)}, {dlt}, {post})"
            if tgt[0] == "member" and tgt[1][0] not in ("this", "super"):
                to = self.stype(tgt[1], cx)
                if to is None or (isinstance(to, str) and short(to) in ("*", "Object")):
                    return f"AS3INC({self._ex(tgt[1], cx)}, {mangle(tgt[2])}, {dlt}, {post})"
            inner = self._ex(tgt, cx)
            return f"({e[1]}{inner})" if k == "pre" else f"({inner}{e[1]})"
        if k == "cond":
            t = self.stype(e[2], cx)
            t2 = self.stype(e[3], cx)
            a, b = self._ex(e[2], cx), self._ex(e[3], cx)
            if t != t2 and not ((t2 == "null" and t not in NUMERIC and t != "Boolean") or
                                (t == "null" and t2 not in NUMERIC and t2 != "Boolean")):
                a, b = f"as3::Any({a})", f"as3::Any({b})"
            return f"(as3::truthy({self._ex(e[1], cx)}) ? {a} : {b})"
        if k == "array":
            return f"as3::arrayOf({{{', '.join('as3::Any(' + self._ex(a, cx) + ')' for a in e[1])}}})"
        if k == "object":
            return f"as3::objectOf({{{', '.join('{' + self.str_lit(kk) + ', as3::Any(' + self._ex(v, cx) + ')}' for kk, v in e[1])}}})"
        if k == "xmlattr":
            return f"as3::xmlAttr({self._ex(e[1], cx)}, \"{e[2]}\")"
        if k == "typeof":
            return f"as3::typeOf({self._ex(e[1], cx)})"
        if k == "delete":
            t = e[1]
            if t[0] == "index":
                return f"as3::deleteKey({self._ex(t[1], cx)}, {self._ex(t[2], cx)})"
            raise Unsupported("delete de algo que no es índice")
        if k == "lambda":
            return self.lambda_(e, cx)
        if k == "type":
            return self.cclass(e[1])
        if k == "super":
            raise Unsupported("super usado como valor")
        raise Unsupported(f"expresión {k}")

    def find_ctor(self, cls):
        while cls in self.infos:
            for m in self.infos[cls].c["members"]:
                if m[0] == "method" and m[1] == cls:
                    return cls, m[2]
            cls = self.infos[cls].base
        return None, []

    def ident(self, n, cx):
        if n in cx.scope:
            return mangle(n)
        # miembro de instancia o estático de la clase actual (o de sus bases)
        for kind, static in (("fields", False), ("getters", None), ("methods", False),
                             ("sfields", True), ("smethods", True)):
            c, v = self.find(self.cls, n, kind)
            if not c:
                continue
            if kind == "getters":
                static = v[1]
                return f"{self.cn(c)}::get_{mangle(n)}()" if static else f"this->get_{mangle(n)}()"
            if kind == "methods":
                return f"as3::bind(this, &{self.cn(c)}::{mangle(n)})"
            if kind == "smethods":
                return f"as3::bindStatic(&{self.cn(c)}::{self.sn(c, n)})"
            return f"{self.cn(c)}::{self.sn(c, n)}" if static else f"this->{mangle(n)}"
        if n in self.infos:
            return f"as3::classOf<{mangle(n)}>()"
        if n in ("NaN",):
            return "as3::NaN"
        if n == "Infinity":
            return "as3::Infinity"
        if n in GLOBAL_FNS:
            return f"as3::fn_{n}"
        if n in AS3_BUILTIN:
            return f"as3::classOf<as3::{n}>()"
        if not cx.static and n[:1] in "abcdefghijklmnopqrstuvwxyz_" and self.inherits_flash(self.cls):
            return f"this->{mangle(n)}"  # miembro heredado de una clase de flash.*
        # clases de flash usadas como valor (p. ej. Event.ENTER_FRAME llega por member)
        self.stubs.add(n)
        return f"flash::{mangle(n)}"

    def inherits_flash(self, cls):
        while cls in self.infos:
            base = self.infos[cls].base
            if base and (base not in self.infos or self.infos[base].flash):
                return True
            cls = base
        return False

    def member(self, e, cx):
        obj, name = e[1], e[2]
        if obj[0] == "super":
            c = self.infos[self.cls].base
            if not c:
                return f"/*super*/{mangle(name)}"
            cg, _g = self.find(c, name, "getters")
            if cg:
                return f"{self.cn(cg)}::get_{mangle(name)}()"
            cf, _f = self.find(c, name, "fields")
            if cf:
                return f"this->{mangle(name)}"
            return f"{self.cn(c)}::{mangle(name)}"
        if obj[0] == "id" and obj[1] in STATIC_HOLDERS and obj[1] not in cx.scope:
            return f"{STATIC_HOLDERS[obj[1]]}::{mangle(name)}"
        ot = self.stype(obj, cx)
        if isinstance(ot, str) and name == "length" and (short(ot) in ("Array", "ByteArray") or ot.startswith("Vector.<")):
            return f"{self._ex(obj, cx)}->get_length()"
        if isinstance(ot, str) and name == "time" and short(ot) == "Date":
            return f"{self._ex(obj, cx)}->getTime()"
        if isinstance(ot, tuple) and ot[0] == "class":
            cname = ot[1]
            if cname in self.infos:
                c, g = self.find(cname, name, "getters")
                if c:
                    return f"{self.cn(c)}::get_{mangle(name)}()"
                c, m = self.find(cname, name, "smethods")
                if c:
                    return f"as3::bindStatic(&{self.cn(c)}::{self.sn(c, name)})"
                c, _ = self.find(cname, name, "sfields")
                return f"{self.cn(c or cname)}::{self.sn(c or cname, name)}"
            if cname == "Math":
                return f"as3::Math::{name}"
            return f"{self.cclass(cname)}::{mangle(name)}"
        if obj[0] == "id" and obj[1] not in cx.scope and not self.member_type(self.cls, obj[1]) \
                and not self.member_type(self.cls, obj[1], static=True) and obj[1][:1].isupper() \
                and obj[1] not in self.infos and obj[1] not in AS3_BUILTIN:
            # Clase de flash.*: Event.ENTER_FRAME, StageAlign.TOP_LEFT...
            self.stubs.add(obj[1])
            return f"flash::{mangle(obj[1])}::{mangle(name)}"
        o = self._ex(obj, cx)
        s = short(ot) if isinstance(ot, str) else None
        if s in ("XML", "XMLList", "Class") and s not in self.infos:
            return f"AS3GET({o}, {mangle(name)})"
        if isinstance(ot, str) and short(ot) == "ByteArray" and name == "bytesAvailable":
            return f"{o}->get_bytesAvailable()"
        if s in self.infos:
            c, g = self.find(s, name, "getters")
            if c:
                return f"{o}->get_{mangle(name)}()"
            c, m = self.find(s, name, "methods")
            if c:
                return f"as3::bind({o}, &{self.cn(c)}::{mangle(name)})"
            c, f = self.find(s, name, "fields")
            if c:
                return f"{o}->{mangle(name)}"
            c, f = self.find(s, name, "setters")
            if c:
                return f"{o}->get_{mangle(name)}()"
            # campo no declarado en la jerarquia conocida (clase dinamica o miembro que flash.h no lista)
            return f"AS3GET({o}, {mangle(name)})"
        if ot and self.is_ref(ot) and s not in ("*", "Object"):
            if s in ("XML", "XMLList", "Class"):
                return f"AS3GET({o}, {mangle(name)})"
            return f"{o}->{mangle(name)}"
        if s == "String":
            return f"{o}.{mangle(name)}()" if name == "length" else f"{o}.{mangle(name)}"
        # acceso dinámico (Object, *, desconocido): se resuelve en C++ según el tipo real de la expresión
        return f"AS3GET({o}, {mangle(name)})"

    @staticmethod
    def has_effects(e):
        if isinstance(e, tuple):
            if e and e[0] in ("pre", "post", "assign"):
                return True
            return any(Emitter.has_effects(x) for x in e[1:])
        if isinstance(e, list):
            return any(Emitter.has_effects(x) for x in e)
        return False

    def call(self, e, cx):
        f, args = e[1], e[2]
        # AS3 evalua los argumentos de izquierda a derecha; C++ no lo garantiza.
        # Si algun argumento tiene efectos (x++, asignacion), se evaluan antes en orden.
        if len(args) >= 2 and any(self.has_effects(a) for a in args):
            self._seq = getattr(self, "_seq", 0) + 1
            pre, tmps = [], []
            for i, a in enumerate(args):
                nm = f"__a{self._seq}_{i}"
                pre.append(f"auto {nm} = {self._ex(a, cx)};")
                tmps.append(("tmp", nm, self.stype(a, cx)))
            inner = self.call((e[0], f, tmps) + tuple(e[3:]), cx)
            return f"([&]() {{ {' '.join(pre)} return {inner}; }}())"
        if f[0] == "super":
            base = self.infos[self.cls].base
            if base in self.infos:
                _, params = self.find_ctor(base)
                return f"{self.cn(base)}::__construct({', '.join(self.args_for(params, args, cx))})"
            return f"{self.cclass(base) if base else 'as3::Object'}::__construct({', '.join(self._ex(a, cx) for a in args)})"
        if f[0] == "member" and f[1][0] == "super":
            base = self.infos[self.cls].base
            c, m = self.find(base, f[2], "methods")
            params = m[1] if m else None
            return f"{mangle(c or base)}::{mangle(f[2])}({', '.join(self.args_for(params, args, cx))})"
        if f[0] == "id":
            n = f[1]
            if n not in cx.scope:
                for kind in ("methods", "smethods"):
                    c, m = self.find(self.cls, n, kind)
                    if c:
                        a = ", ".join(self.args_for(m[1], args, cx))
                        return f"this->{mangle(n)}({a})" if kind == "methods" else f"{self.cn(c)}::{self.sn(c, n)}({a})"
                if n in ("int", "uint", "Number", "String", "Boolean") and len(args) == 1:
                    return self.conv(self._ex(args[0], cx), self.stype(args[0], cx) or "*", n)
                if (n in self.infos or n in AS3_BUILTIN) and len(args) == 1 and n != "Array":
                    return f"as3::cast<{self.cclass(n)}>({self._ex(args[0], cx)})"
                if n in GLOBAL_FNS:
                    return f"as3::fn_{n}({', '.join(self._ex(a, cx) for a in args)})"
                if not cx.static and n[:1] in "abcdefghijklmnopqrstuvwxyz_" and self.inherits_flash(self.cls) \
                        and not self.member_type(self.cls, n):
                    return f"this->{mangle(n)}({', '.join(self._ex(a, cx) for a in args)})"
        if f[0] == "member" and f[1][0] == "id" and f[1][1] in STATIC_HOLDERS and f[1][1] not in cx.scope:
            return f"{STATIC_HOLDERS[f[1][1]]}::{mangle(f[2])}({', '.join(self._ex(a, cx) for a in args)})"
        if f[0] == "vectype":
            return f"as3::toVector<{self.ctype(f[1])}>({', '.join(self._ex(a, cx) for a in args)})"
        if f[0] == "member":
            ot = self.stype(f[1], cx)
            s = short(ot) if isinstance(ot, str) else None
            static_cls = ot[1] if isinstance(ot, tuple) and ot[0] == "class" else None
            if s in ("int", "uint", "Number", "Boolean") and f[2] in ("toString", "toFixed", "toPrecision", "toExponential", "valueOf"):
                return f"as3::num_{f[2]}({self._ex(f[1], cx)}{''.join(', ' + self._ex(a, cx) for a in args)})"
            if static_cls in self.infos:
                c, m = self.find(static_cls, f[2], "smethods")
                if c:
                    return f"{self.cn(c)}::{self.sn(c, f[2])}({', '.join(self.args_for(m[1], args, cx))})"
                cf2, _ft2 = self.find(static_cls, f[2], "sfields")
                if cf2:
                    return f"as3::invoke({self.cn(cf2)}::{self.sn(cf2, f[2])}, {{{', '.join('as3::Any(' + self._ex(a, cx) + ')' for a in args)}}})"
                return f"{self.cn(static_cls)}::{mangle(f[2])}({', '.join(self._ex(a, cx) for a in args)})"
            elif static_cls == "Math":
                return f"as3::Math::{f[2]}({', '.join(self._ex(a, cx) for a in args)})"
            elif static_cls:
                return f"{self.cclass(static_cls)}::{mangle(f[2])}({', '.join(self._ex(a, cx) for a in args)})"
            if s in self.infos:
                c, m = self.find(s, f[2], "methods")
                if c:
                    return f"{self._ex(f[1], cx)}->{mangle(f[2])}({', '.join(self.args_for(m[1], args, cx))})"
                cf_, _ft = self.find(s, f[2], "fields")
                if not cf_ and not self.find(s, f[2], "getters")[0]:
                    # no esta en la tabla de tipos (p. ej. metodo de flash.h con plantilla): se deja al compilador de C++
                    return f"{self._ex(f[1], cx)}->{mangle(f[2])}({', '.join(self._ex(a, cx) for a in args)})"
            if s == "String":
                return f"{self._ex(f[1], cx)}.{mangle(f[2])}({', '.join(self._ex(a, cx) for a in args)})"
            if ot and self.is_ref(ot) and s in ("XML", "XMLList"):
                return f"AS3CALL({self._ex(f[1], cx)}, {mangle(f[2])}{''.join(', ' + self._ex(a, cx) for a in args)})"
            if ot and self.is_ref(ot) and s not in ("*", "Object") and s not in self.infos:
                return f"{self._ex(f[1], cx)}->{mangle(f[2])}({', '.join(self._ex(a, cx) for a in args)})"
            if s is None or s in ("*", "Object"):
                if f[1][0] == "id" and f[1][1][:1].isupper() and f[1][1] not in cx.scope \
                        and not self.member_type(self.cls, f[1][1]) and not self.member_type(self.cls, f[1][1], True):
                    self.stubs.add(f[1][1])
                    return f"flash::{mangle(f[1][1])}::{mangle(f[2])}({', '.join(self._ex(a, cx) for a in args)})"
                return f"AS3CALL({self._ex(f[1], cx)}, {mangle(f[2])}{''.join(', ' + self._ex(a, cx) for a in args)})"
        # valor Function (variable, campo o índice)
        return f"as3::invoke({self._ex(f, cx)}, {{{', '.join('as3::Any(' + self._ex(a, cx) + ')' for a in args)}}})"

    def is_setter_target(self, t, cx):
        if t[0] == "member" and t[1][0] != "super":
            ot = self.stype(t[1], cx)
            if isinstance(ot, tuple) and ot[0] == "class":
                c, _ = self.find(ot[1], t[2], "setters")
                return bool(c)
            s = short(ot) if isinstance(ot, str) else None
            if s in self.infos:
                c, _ = self.find(s, t[2], "setters")
                cf, _ = self.find(s, t[2], "fields")
                return bool(c) and not cf
        if t[0] == "id" and t[1] not in cx.scope:
            c, _ = self.find(self.cls, t[1], "setters")
            return bool(c)
        return False

    def assign(self, e, cx):
        op, lhs, rhs = e[1], e[2], e[3]
        lt = self.stype(lhs, cx)
        if self.is_setter_target(lhs, cx):
            if lhs[0] == "member":
                ot = self.stype(lhs[1], cx)
                if isinstance(ot, tuple):
                    c, st = self.find(ot[1], lhs[2], "setters")
                    target = f"{self.cn(c)}::set_{mangle(lhs[2])}"
                else:
                    c, st = self.find(short(ot), lhs[2], "setters")
                    target = f"{self._ex(lhs[1], cx)}->set_{mangle(lhs[2])}"
            else:
                c, st = self.find(self.cls, lhs[1], "setters")
                target = f"{self.cn(c)}::set_{mangle(lhs[1])}" if st[1] else f"this->set_{mangle(lhs[1])}"
            val = rhs if op == "=" else ("bin", op[:-1], lhs, rhs)
            return f"{target}({self.ex(val, cx, st[0])})"
        if lhs[0] == "index":
            if op != "=":
                val = ("bin", op[:-1], lhs, rhs)
            else:
                val = rhs
            return f"as3::setAt({self._ex(lhs[1], cx)}, {self._ex(lhs[2], cx)}, as3::Any({self._ex(val, cx)}))"
        if lhs[0] == "member" and lhs[1][0] == "super":
            sb = self.infos[self.cls].base
            cs, sst = self.find(sb, lhs[2], "setters") if sb else (None, None)
            if cs:
                val = rhs if op == "=" else ("bin", op[:-1], lhs, rhs)
                return f"{self.cn(cs)}::set_{mangle(lhs[2])}({self.ex(val, cx, sst[0])})"
        if lhs[0] == "member" and lhs[2] == "time":
            lo = self.stype(lhs[1], cx)
            if isinstance(lo, str) and short(lo) == "Date":
                val = rhs if op == "=" else ("bin", op[:-1], lhs, rhs)
                return f"{self._ex(lhs[1], cx)}->setTime({self.ex(val, cx, 'Number')})"
        if lhs[0] == "member" and lhs[2] == "length":
            lo = self.stype(lhs[1], cx)
            if isinstance(lo, str) and (short(lo) in ("Array", "ByteArray") or lo.startswith("Vector.<")):
                val = rhs if op == "=" else ("bin", op[:-1], lhs, rhs)
                return f"{self._ex(lhs[1], cx)}->set_length({self.ex(val, cx, 'Number')})"
        _dt = self.stype(lhs[1], cx) if lhs[0] == "member" else 0
        if lhs[0] == "member" and (_dt is None or (isinstance(_dt, str) and short(_dt) in ("*", "Object"))) \
                and lhs[1][0] not in ("this", "super"):
            o = self._ex(lhs[1], cx)
            if not o.startswith("flash::"):
                val = rhs if op == "=" else ("bin", op[:-1], lhs, rhs)
                return f"AS3SET({o}, {mangle(lhs[2])}, {self._ex(val, cx)})"
        if lhs[0] == "member" and lhs[1][0] != "super":
            ot_ = self.stype(lhs[1], cx)
            s_ = short(ot_) if isinstance(ot_, str) else None
            if s_ in ("XML", "XMLList") or (s_ in self.infos and not any(self.find(s_, lhs[2], k)[0] for k in ("fields", "getters", "setters", "methods"))):
                val = rhs if op == "=" else ("bin", op[:-1], lhs, rhs)
                return f"AS3SET({self._ex(lhs[1], cx)}, {mangle(lhs[2])}, {self._ex(val, cx)})"
        l = self._ex(lhs, cx)
        if op in ("&&=", "||="):
            return f"({l} = {self.ex(('bin', op[:2], lhs, rhs), cx, lt)})"
        if op == ">>>=":
            return f"({l} = as3::ushr({l}, {self._ex(rhs, cx)}))"
        if op == "=":
            return f"{l} = {self.ex(rhs, cx, lt)}"
        if op == "+=" and short(lt) == "String":
            return f"{l} = as3::add({l}, {self._ex(rhs, cx)})"
        if lt in ("int", "uint") and op in ("+=", "-=", "*=") and self.stype(rhs, cx) not in ("int", "uint"):
            return f"{l} = {self.ex(('bin', op[:-1], lhs, rhs), cx, lt)}"
        if lt in ("int", "uint") and op == "/=":
            return f"{l} = {self.ex(('bin', '/', lhs, rhs), cx, lt)}"
        if op == "%=" and lt == "Number":
            return f"{l} = as3::fmod({l}, {self._ex(rhs, cx)})"
        if lt is None or short(lt) in ("*", "Object"):
            return f"{l} = as3::Any({self.ex(('bin', op[:-1], lhs, rhs), cx)})"
        return f"{l} {op} {self.ex(rhs, cx, lt if lt in NUMERIC else None)}"

    def binop(self, e, cx):
        op, a, b = e[1], e[2], e[3]
        if op in ("is", "as") and "." not in b[1] and b[1] in cx.scope and cx.scope[b[1]] in ("Class", "*", None):
            if op == "is":
                return f"as3::isDyn({self._ex(a, cx)}, {mangle(b[1])})"
            return f"({self._ex(a, cx)})"
        if op in ("is", "as") and "." in b[1]:
            parts = b[1].split(".")
            if parts[0] in cx.scope or parts[0][:1].islower() or self.member_type(self.cls, parts[0]):
                ex_ = ("id", parts[0])
                for p_ in parts[1:]:
                    ex_ = ("member", ex_, p_)
                if op == "is":
                    return f"as3::isDyn({self._ex(a, cx)}, {self._ex(ex_, cx)})"
                return f"({self._ex(a, cx)})"
        if op == "is":
            return f"as3::is<{self.cclass(b[1])}>({self._ex(a, cx)})"
        if op == "as":
            cc = self.cclass(b[1])
            if cc in ("double", "int32_t", "uint32_t", "bool", "as3::String"):
                return f"as3::asPrim<{cc}>({self._ex(a, cx)})"
            return f"as3::as<{cc}>({self._ex(a, cx)})"
        if op == "instanceof":
            return f"as3::instanceOf({self._ex(a, cx)}, {self._ex(b, cx)})"
        if op == "in":
            return f"as3::hasKey({self._ex(b, cx)}, {self._ex(a, cx)})"
        ta, tb = self.stype(a, cx), self.stype(b, cx)
        A, B = self._ex(a, cx), self._ex(b, cx)
        if op in ("&&", "||"):
            if ta == tb == "Boolean":
                return f"({A} {op} {B})"
            if ta == tb and ta is not None:
                return f"as3::{'land' if op == '&&' else 'lor'}({A}, [&]{{ return {B}; }})"
            return f"as3::{'land' if op == '&&' else 'lor'}(as3::Any({A}), [&]{{ return as3::Any({B}); }})"
        if op == "+":
            if ta in NUMERIC and tb in NUMERIC:
                return f"({A} + {B})" if not (ta == tb == "int" or ta == tb == "uint") else f"(double({A}) + {B})"
            return f"as3::add({A}, {B})"
        if op in ("-", "*"):
            if ta in NUMERIC and tb in NUMERIC:
                return f"(double({A}) {op} {B})" if ta != "Number" and tb != "Number" else f"({A} {op} {B})"
            return f"(as3::toNumber({A}) {op} as3::toNumber({B}))"
        if op == "/":
            return f"(as3::toNumber({A}) / as3::toNumber({B}))" if not (ta in NUMERIC and tb in NUMERIC) else f"(double({A}) / {B})"
        if op == "%":
            return f"as3::fmod({A}, {B})"
        if op in ("&", "|", "^", "<<", ">>"):
            return f"(as3::toInt({A}) {op} as3::toInt({B}))" if op not in ("<<", ">>") else \
                f"as3::{'shl' if op == '<<' else 'shr'}({A}, {B})"
        if op == ">>>":
            return f"as3::ushr({A}, {B})"
        if op in ("==", "!=", "===", "!=="):
            strict = len(op) == 3
            neg = op.startswith("!")
            if ta == tb and ta in ("int", "uint", "Boolean", "Number"):
                return f"({A} {'!=' if neg else '=='} {B})"
            if (ta in NUMERIC and tb in NUMERIC):
                return f"(double({A}) {'!=' if neg else '=='} double({B}))"
            fn = "strictEq" if strict else "eq"
            return f"({'!' if neg else ''}as3::{fn}({A}, {B}))"
        if op in ("<", ">", "<=", ">="):
            if ta in NUMERIC and tb in NUMERIC:
                if ta != tb and "Number" not in (ta, tb):
                    return f"(double({A}) {op} double({B}))"
                return f"({A} {op} {B})"
            return f"as3::cmp_{ {'<': 'lt', '>': 'gt', '<=': 'le', '>=': 'ge'}[op] }({A}, {B})"
        raise Unsupported(f"operador {op}")

    def lambda_(self, e, cx):
        _, params, ret, body, rest = e
        inner = Ctx(dict(cx.scope), cx.static)
        for n, t, _ in params:
            inner.scope[n] = t
        lines = []
        for i, (n, t, d) in enumerate(params):
            dv = self.ex(d, cx, t) if d else self.default(t) if t not in ("*", None) else "as3::undefined"
            lines.append(f"{self.ctype(t)} {mangle(n)} = a.size() > {i} ? {self.conv(f'a[{i}]', '*', t)} : {dv};")
        if rest:
            inner.scope[rest] = "Array"
            lines.append(f"as3::Array* {mangle(rest)} = as3::restArgs(a, {len(params)});")
        body_lines = self.st(body, inner, 0, ret_t=ret, in_lambda=True)
        cap = "[=]" if not cx.static else "[=]"
        return f"as3::Function({cap}(const std::vector<as3::Any>& a) -> as3::Any {{ " + " ".join(lines) + " " + \
               " ".join(l.strip() for l in body_lines) + " return as3::undefined; })"

    # ---------- sentencias
    def st(self, s, cx, ind, ret_t=None, in_lambda=False):
        out = self.st_(s, cx, ind, ret_t, in_lambda)
        if out and s[0] in ("expr", "var", "return", "if", "while", "for", "forin", "throw", "dowhile", "switch"):
            self.line_id = getattr(self, "line_id", 0) + 1
            lm = getattr(self, "line_map", None)
            if lm is None:
                lm = self.line_map = {}
            lm[self.line_id] = f"{self.cls}.{getattr(self, 'curfn', '?')}: {out[0].strip()[:150]}"
            out = ["    " * ind + f"AS3_L({self.line_id});"] + out
        return out

    def st_(self, s, cx, ind, ret_t=None, in_lambda=False):
        p = "    " * ind
        k = s[0]
        self.ret_t = ret_t if ret_t is not None else getattr(self, "ret_t", None)
        self.in_lambda = in_lambda
        if k == "block":
            saved = dict(cx.scope)
            out = [p + "{"] + [l for x in s[1] for l in self.st(x, cx, ind + 1, ret_t, in_lambda)] + [p + "}"]
            # AS3 tiene ámbito de función para var; C++ no. Las vars se elevan (ver hoist).
            cx.scope.update(saved | cx.scope)
            return out
        if k == "empty":
            return []
        if k == "var":
            out = []
            for n, ty, init in s[1]:
                cx.scope[n] = ty
                if init is not None:
                    out.append(f"{p}{mangle(n)} = {self.ex(init, cx, ty)};")
            return out
        if k == "expr":
            return [f"{p}{self._ex(s[1], cx)};"]
        if k == "return":
            if s[1] is None:
                return [f"{p}return{' as3::undefined' if in_lambda else ''};"]
            want = "*" if in_lambda else ret_t
            v = self.ex(s[1], cx, want if want not in (None, "void") else None)
            if in_lambda:
                v = f"as3::Any({v})"
            return [f"{p}return {v};"]
        if k == "if":
            out = [f"{p}if ({self.tr(s[1], cx)})"] + self.blk(s[2], cx, ind, ret_t, in_lambda)
            if s[3]:
                out += [f"{p}else"] + self.blk(s[3], cx, ind, ret_t, in_lambda)
            return out
        if k == "while":
            return self.loop(f"{p}while ({self.tr(s[1], cx)})", s[2], cx, ind, ret_t, in_lambda)
        if k == "dowhile":
            body = self.loop(f"{p}do", s[1], cx, ind, ret_t, in_lambda)
            return body + [f"{p}while (as3::truthy({self._ex(s[2], cx)}));"]
        if k == "for":
            init = ""
            if s[1]:
                if s[1][0] == "var":
                    parts = []
                    for n, ty, iv in s[1][1]:
                        cx.scope[n] = ty
                        if iv is not None:
                            parts.append(f"{mangle(n)} = {self.ex(iv, cx, ty)}")
                    init = ", ".join(parts)
                else:
                    init = self._ex(s[1][1], cx)
            c = f"as3::truthy({self._ex(s[2], cx)})" if s[2] else ""
            step = self._ex(s[3], cx) if s[3] else ""
            return self.loop(f"{p}for ({init}; {c}; {step})", s[4], cx, ind, ret_t, in_lambda)
        if k == "forin":
            _, each, n, ty, decl, obj, body = s
            if decl:
                cx.scope[n] = ty
            vt = cx.scope.get(n) or self.stype(("id", n), cx)
            fn = "eachValue" if each else "eachKey"
            tmp = f"__it{self.tmpn()}"
            head = f"{p}for (const as3::Any& {tmp} : as3::{fn}({self._ex(obj, cx)}))"
            assign = f"{p}    {self.ident(n, cx)} = {self.conv(tmp, '*', vt)};"
            inner = self.loop("", body, cx, ind, ret_t, in_lambda)
            # inner[0] es la cabecera vacía; insertar la asignación tras '{'
            inner = [head, inner[1], assign] + inner[2:]
            return inner
        if k == "switch":
            v = self._ex(s[1], cx)
            tmp = f"__sw{self.tmpn()}"
            lab = f"__swend{self.tmpn()}"
            out = [f"{p}{{", f"{p}    as3::Any {tmp} = as3::Any({v});", f"{p}    int __case = -1;"]
            conds = []
            for i, (e, _) in enumerate(s[2]):
                if e is not None:
                    conds.append(f"{p}    if (__case < 0 && as3::strictEq({tmp}, as3::Any({self._ex(e, cx)}))) __case = {i};")
            dflt = next((i for i, (e, _) in enumerate(s[2]) if e is None), None)
            out += conds
            if dflt is not None:
                out.append(f"{p}    if (__case < 0) __case = {dflt};")
            out.append(f"{p}    switch (__case)")
            out.append(f"{p}    {{")
            cx.labels.append(("switch", None))
            for i, (_, body) in enumerate(s[2]):
                out.append(f"{p}    case {i}:")
                out.append(f"{p}    {{")
                for x in body:
                    out += self.st(x, cx, ind + 2, ret_t, in_lambda)
                out.append(f"{p}    }}")
            cx.labels.pop()
            out += [f"{p}    }}", f"{p}}}"]
            return out
        if k == "label":
            lab = mangle(s[1])
            cx.labels.append(("label", lab))
            inner = s[2]
            if inner[0] in ("for", "while", "dowhile", "forin"):
                cx.pending_label = lab
                out = self.st(inner, cx, ind, ret_t, in_lambda)
            else:
                out = self.st(inner, cx, ind, ret_t, in_lambda)
            cx.labels.pop()
            return out + [f"{p}{lab}_brk:;"]
        if k == "break":
            if s[1]:
                return [f"{p}goto {mangle(s[1])}_brk;"]
            return [f"{p}break;"]
        if k == "continue":
            if s[1]:
                return [f"{p}goto {mangle(s[1])}_cnt;"]
            return [f"{p}continue;"]
        if k == "throw":
            return [f"{p}throw as3::Thrown(as3::Any({self._ex(s[1], cx)}));"]
        if k == "try":
            out = [f"{p}try"] + self.st(s[1], cx, ind, ret_t, in_lambda)
            for n, ty, body in s[2]:
                cx.scope[n] = ty
                out.append(f"{p}catch (const as3::Thrown& __ex)")
                out.append(f"{p}{{")
                if ty not in ("*", "Error", None) and short(ty) != "Error":
                    out.append(f"{p}    if (!as3::is<{self.cclass(ty)}>(__ex.value)) throw;")
                out.append(f"{p}    {self.ctype(ty)} {mangle(n)} = {self.conv('__ex.value', '*', ty)};")
                out += self.st(body, cx, ind + 1, ret_t, in_lambda)
                out.append(f"{p}}}")
            if s[3]:
                fin = self.st(s[3], cx, ind + 1, ret_t, in_lambda)
                out = [f"{p}{{", f"{p}    auto __fin{self.tmpn()} = as3::onScopeExit([&]()"] + fin + \
                      [f"{p}    );"] + ["    " + l for l in out] + [f"{p}}}"]
            return out
        if k == "localfn":
            _, n, params, ret, body, rest = s
            cx.scope[n] = "Function"
            return [f"{p}{mangle(n)} = {self.lambda_(('lambda', params, ret, body, rest), cx)};"]
        raise Unsupported(f"sentencia {k}")

    def tmpn(self):
        self._tmp = getattr(self, "_tmp", 0) + 1
        return self._tmp

    def loop(self, head, body, cx, ind, ret_t, in_lambda):
        p = "    " * ind
        lab = getattr(cx, "pending_label", None)
        cx.pending_label = None
        b = self.blk(body, cx, ind, ret_t, in_lambda)
        if lab:
            # continue etiquetado: salta al final del cuerpo
            b = b[:-1] + [f"{p}    {lab}_cnt:;", b[-1]]
        return [head] + b

    def tr(self, e, cx):
        """as3::truthy(expr); si la expresion es una llamada que devuelve void (fallo del decompilador), se evalua y es false."""
        if self.stype(e, cx) == "void":
            return f"(({self._ex(e, cx)}), false)"
        return f"as3::truthy({self._ex(e, cx)})"

    def blk(self, s, cx, ind, ret_t=None, in_lambda=False):
        return self.st(s if s[0] == "block" else ("block", [s]), cx, ind, ret_t, in_lambda)

    def hoist(self, body, scope):
        """AS3: var tiene ámbito de función. Devuelve [(nombre, tipo)] a declarar al inicio."""
        found = []

        def walk(s):
            if not isinstance(s, tuple):
                return
            k = s[0]
            if k == "var":
                for n, ty, _ in s[1]:
                    if n not in scope and n not in [f[0] for f in found]:
                        found.append((n, ty))
            elif k == "forin" and s[4]:
                if s[2] not in scope and s[2] not in [f[0] for f in found]:
                    found.append((s[2], s[3]))
            elif k == "localfn":
                if s[1] not in [f[0] for f in found]:
                    found.append((s[1], "Function"))
            if k == "lambda":
                return
            for x in s[1:]:
                if isinstance(x, tuple):
                    walk(x)
                elif isinstance(x, list):
                    for y in x:
                        if isinstance(y, tuple):
                            walk(y)
                        elif isinstance(y, (list, tuple)):
                            pass

        def walk_all(s):
            walk(s)
            if isinstance(s, tuple) and s and s[0] == "switch":
                for _, bl in s[2]:
                    for x in bl:
                        walk_all(x)
            if isinstance(s, tuple) and s and s[0] == "try":
                for _, _, bl in s[2]:
                    walk_all(bl)

        def rec(s):
            if isinstance(s, tuple) and s and isinstance(s[0], str):
                walk_all(s)
                if s[0] in ("block",):
                    for x in s[1]:
                        rec(x)
                elif s[0] == "if":
                    rec(s[2]); s[3] and rec(s[3])
                elif s[0] in ("while",):
                    rec(s[2])
                elif s[0] == "dowhile":
                    rec(s[1])
                elif s[0] == "for":
                    s[1] and rec(s[1]); rec(s[4])
                elif s[0] == "forin":
                    rec(s[6])
                elif s[0] == "label":
                    rec(s[2])
                elif s[0] == "switch":
                    for _, bl in s[2]:
                        for x in bl:
                            rec(x)
                elif s[0] == "try":
                    rec(s[1])
                    for _, _, bl in s[2]:
                        rec(bl)
                    s[3] and rec(s[3])
        rec(body)
        return found

    # ---------- clase
    def klass(self, c):
        name = c["name"]
        self.cls = name
        ci = self.translated[name]
        cn = mangle(name)
        base = ci.base
        ifs = [i for i in ci.ifaces if i in self.translated]
        if c["iface"]:
            H = ["#pragma once", '#include "as3rt.h"', '#include "classes.h"'] + [f'#include "{mangle(i)}.h"' for i in ifs]
            H += ["", f"struct {cn}" + ((" : " + ", ".join(f"public {mangle(i)}" for i in ifs)) if ifs else ""), "{",
                  f"    virtual ~{cn}() = default;"]
            for m in c["members"]:
                if m[0] == "method":
                    _, n, params, ret, _body, _mods, acc, rest = m
                    mn = ("get_" if acc == "get" else "set_" if acc == "set" else "") + mangle(n)
                    rt = "void" if acc == "set" else self.ctype(ret)
                    H.append(f"    virtual {rt} {mn}({self.param_list(params, rest, True)}) = 0;")
            H.append("};")
            return "\n".join(H) + "\n", f'#include "all.h"\n// interfaz {name}: solo declaraciones\n'
        if base and base in AS3_BUILTIN and base not in self.infos:
            base_c = f"as3::{base}"
        elif base and base not in self.infos:
            self.stubs.add(base)
            base_c = f"flash::{base}"
        elif base and self.infos[base].flash:
            base_c = f"flash::{base}"
        elif base:
            base_c = mangle(base)
        else:
            base_c = "as3::Object"
        H = ["#pragma once", '#include "as3rt.h"', '#include "classes.h"']
        if base and base in self.translated and not self.infos[base].flash:
            H.append(f'#include "{mangle(base)}.h"')
        H += [f'#include "{mangle(i)}.h"' for i in ifs]
        H += ["", f"struct {cn} : public {base_c}" + "".join(f", public {mangle(i)}" for i in ifs), "{",
              f"    using __base = {base_c};"]
        C = ['#include "all.h"', ""]
        if c.get("embed"):
            H.append(f"    static constexpr const char* __embed = {self.str_lit(c['embed'].get('source', ''))};")
        ctor = None
        finit = []  # asignaciones de los estaticos con inicializador, se ejecutan en __init_statics()
        # El decompilador deja las variables locales del inicializador estatico (_temp_N, _locN_) como campos de
        # instancia en el cuerpo de la clase: aqui vuelven a ser locales de __static_init.
        cinit = []
        if c["sinit"]:
            cinit = [m for m in c["members"] if m[0] == "field" and "static" not in m[4] and re.fullmatch(r"_temp_\d+|_loc\d+_", m[1])]
        cinit_names = {m[1] for m in cinit}
        for m in c["members"]:
            if m[0] == "field" and m[1] in cinit_names:
                continue
            if m[0] == "field":
                _, n, ty, init, mods, is_const = m
                if "static" in mods:
                    H.append(f"    static {self.ctype(ty)} {self.sn(name, n)};")
                    if not hasattr(self, "static_fields"):
                        self.static_fields = []
                    self.static_fields.append(f"{cn}::{self.sn(name, n)}")
                    cx = Ctx({}, True)
                    C.append(f"{self.ctype(ty)} {cn}::{self.sn(name, n)} = {self.default(ty)};")
                    if init is not None:
                        finit.append(f"    {cn}::{self.sn(name, n)} = {self.ex(init, cx, ty)};")
                else:
                    H.append(f"    {self.ctype(ty)} {mangle(n)} = {self.default(ty)};")
            elif m[1] == name and not m[6]:
                ctor = m
        H.append("")
        C.append("")
        # métodos
        for m in c["members"]:
            if m[0] != "method" or m is ctor:
                continue
            _, n, params, ret, body, mods, acc, rest = m
            static = "static" in mods
            mn = ("get_" if acc == "get" else "set_" if acc == "set" else "") + (self.sn(name, n) if static and not acc else mangle(n))
            rt = "void" if acc == "set" else self.ctype(ret)
            decl_ps = self.param_list(params, rest, with_defaults=True)
            def_ps = self.param_list(params, rest, with_defaults=False)
            H.append(f"    {'static' if static else 'virtual'} {rt} {mn}({decl_ps});")
            if body is None:
                C.append(f"{rt} {cn}::{mn}({def_ps}) {{ as3::notImplemented(\"{name}.{n}\"); }}")
                continue
            C += self.method_body(f"{rt} {cn}::{mn}({def_ps})", params, rest, ret if acc != "set" else "void",
                                  body, static, pre=[])
        # constructor AS3
        params, rest, body = (ctor[2], ctor[7], ctor[4]) if ctor else ([], None, ("block", []))
        H.append(f"    void __construct({self.param_list(params, rest, True)});")
        pre = []
        cx0 = Ctx({n: t for n, t, _ in params}, False)
        for m in c["members"]:
            if m[0] == "field" and "static" not in m[4] and m[3] is not None and m[1] not in cinit_names:
                pre.append(f"    this->{mangle(m[1])} = {self.ex(m[3], cx0, m[2])};")
        has_super = any(x[0] == "expr" and x[1][0] == "call" and x[1][1][0] == "super" for x in body[1])
        if not has_super and base:
            pre.append(f"    {base_c}::__construct();")
        if c.get("embed"):
            pre.append("    this->__loadEmbed(__embed);")
        C += self.method_body(f"void {cn}::__construct({self.param_list(params, rest, False)})",
                              params, rest, "void", body, False, pre=pre)
        # inicializador estático
        if c["sinit"]:
            H.append("    static void __static_init();")
            C += self.method_body(f"void {cn}::__static_init()", [], None, "void",
                                  ("block", [("var", [(m[1], m[2], m[3])]) for m in cinit] + c["sinit"]), True, pre=[])
        body_init = finit + ([f"    {cn}::__static_init();"] if c["sinit"] else [])
        if body_init:
            H.append("    static void __init_statics();")
            C += [f"void {cn}::__init_statics()", "{"] + body_init + ["}", ""]
            text = "\n".join(body_init) + "\n".join(C)
            self.static_deps[name] = {d for d in re.findall(r"\b([A-Za-z_]\w*)::", text) if d != cn}
        # reflexion: acceso por nombre a campos, propiedades y metodos (el juego hace obj[nombre] y obj[nombre](...))
        gets, sets, calls, has = [], [], [], []
        for m in c["members"]:
            if m[0] == "field" and "static" not in m[4] and m[1] not in cinit_names:
                fn_, fty = m[1], m[2]
                gets.append(f'    if (n == "{fn_}") return as3::Any(this->{mangle(fn_)});')
                if not m[5]:
                    sets.append(f'    if (n == "{fn_}") {{ this->{mangle(fn_)} = as3::fromAny<{self.ctype(fty)}>(v); return; }}')
                has.append(f'n == "{fn_}"')
            elif m[0] == "method" and m[1] != name and "static" not in m[5]:
                _, mn_, mparams, mret, _b, _mods, macc, _r = m
                if macc == "get":
                    gets.append(f'    if (n == "{mn_}") return as3::Any(this->get_{mangle(mn_)}());')
                    has.append(f'n == "{mn_}"')
                elif macc == "set":
                    sets.append(f'    if (n == "{mn_}") {{ this->set_{mangle(mn_)}(as3::fromAny<{self.ctype(mparams[0][1]) if mparams else "as3::Any"}>(v)); return; }}')
                else:
                    calls.append(f'    if (n == "{mn_}") return as3::invokeMember(this, &{cn}::{mangle(mn_)}, a);')
                    # obj["metodo"] devuelve el metodo enlazado (como en AS3)
                    gets.append(f'    if (n == "{mn_}") return as3::Any(as3::bind(this, &{cn}::{mangle(mn_)}));')
                    has.append(f'n == "{mn_}"')
        # recolector: marca los objetos de los campos de instancia
        tr = [f"    as3::gc::mark(this->{mangle(m[1])});" for m in c["members"]
              if m[0] == "field" and "static" not in m[4] and m[1] not in cinit_names]
        H.append("    void __trace() override;")
        C += [f"void {cn}::__trace()", "{", f"    {base_c}::__trace();"] + tr + ["}", ""]
        # guardado (SharedObject): campos publicos de instancia, los que serializaba AMF
        pub = [m[1] for m in c["members"] if m[0] == "field" and "static" not in m[4] and "public" in m[4] and m[1] not in cinit_names]
        H.append("    void __fields(std::vector<std::string>& out) override;")
        C += [f"void {cn}::__fields(std::vector<std::string>& out)", "{", f"    {base_c}::__fields(out);"] + \
             [f'    out.push_back("{n_}");' for n_ in pub] + ["}", ""]
        H.append("    as3::Any __get(const std::string& n) override;")
        H.append("    void __set(const std::string& n, const as3::Any& v) override;")
        H.append("    bool __has(const std::string& n) override;")
        H.append("    as3::Any __call(const std::string& n, const std::vector<as3::Any>& a) override;")
        H.append(f'    as3::String __className() const override {{ return as3::String("{name}"); }}')
        C += [f"as3::Any {cn}::__get(const std::string& n)", "{"] + gets + [f"    return {base_c}::__get(n);", "}", ""]
        C += [f"void {cn}::__set(const std::string& n, const as3::Any& v)", "{"] + sets + [f"    {base_c}::__set(n, v);", "}", ""]
        C += [f"bool {cn}::__has(const std::string& n)", "{", "    if (" + (" || ".join(has) if has else "false") + ") return true;",
              f"    return {base_c}::__has(n);", "}", ""]
        C += [f"as3::Any {cn}::__call(const std::string& n, const std::vector<as3::Any>& a)", "{"] + calls + [f"    return {base_c}::__call(n, a);", "}", ""]
        H.append("};")
        return "\n".join(H) + "\n", "\n".join(C) + "\n"

    def param_list(self, params, rest, with_defaults):
        out = []
        for n, t, d in params:
            s = f"{self.ctype(t)} {mangle(n)}"
            if with_defaults and d is not None:
                s += f" = {self.ex(d, Ctx({}, True), t)}"
            elif with_defaults and out and "=" in out[-1]:
                s += f" = {self.default(t)}"
            out.append(s)
        if rest:
            out.append(f"as3::Array* {mangle(rest)}" + (" = nullptr" if with_defaults else ""))
        return ", ".join(out)

    # ---------- inferencia de tipos de variables locales `*` (el decompilador pierde los tipos de contadores y acumuladores)
    def infer_locals(self, hoisted, body, cx):
        cand = {n for n, t in hoisted if t in ("*", None)}
        if not cand:
            return hoisted
        assigns = {n: [] for n in cand}
        blocked = set()

        def walk(x):
            if isinstance(x, list):
                for y in x:
                    walk(y)
                return
            if not isinstance(x, tuple) or not x or not isinstance(x[0], str):
                return
            k = x[0]
            if k == "var":
                for n, ty, init in x[1]:
                    if n in cand:
                        if ty not in ("*", None):
                            blocked.add(n)
                        elif init is not None:
                            assigns[n].append(init)
                    if init is not None:
                        walk(init)
                return
            if k == "assign" and x[2][0] == "id" and x[2][1] in cand:
                n = x[2][1]
                assigns[n].append(x[3] if x[1] == "=" else ("bin", x[1][:-1], x[2], x[3]))
            elif k in ("pre", "post") and x[2][0] == "id" and x[2][1] in cand:
                assigns[x[2][1]].append(("incdec",))
            elif k == "forin":
                if x[2] in cand:
                    blocked.add(x[2])
            elif k == "try":
                for cn_, _ct, _cb in x[2]:
                    if cn_ in cand:
                        blocked.add(cn_)
            elif k == "localfn" and x[1] in cand:
                blocked.add(x[1])
            for y in x[1:]:
                if isinstance(y, (tuple, list)):
                    walk(y)

        walk(body)

        def is_int(e, assume):
            k = e[0]
            if k == "incdec":
                return True
            if k == "num":
                return bool(re.fullmatch(r"\d+", e[1])) and int(e[1]) <= 0x7fffffff
            if k == "id":
                return e[1] in assume or cx.scope.get(e[1]) == "int"
            if k == "paren":
                return is_int(e[1], assume)
            if k == "un":
                return e[1] in ("~",) or (e[1] == "-" and is_int(e[2], assume))
            if k == "bin":
                if e[1] in ("&", "|", "^", "<<", ">>"):
                    return True
                if e[1] in ("+", "-", "%"):
                    return is_int(e[2], assume) and is_int(e[3], assume)
                return False
            if k == "cond":
                return is_int(e[2], assume) and is_int(e[3], assume)
            return self.stype(e, cx) == "int"

        types = {}
        for _ in range(4):
            progress = False
            for n in sorted(cand - blocked):
                if n in types or not assigns[n]:
                    continue
                es = assigns[n]
                # 1) entero: todo lo asignado es entero (el propio contador cuenta como entero)
                if all(is_int(e, {n}) for e in es):
                    types[n] = "int"
                    cx.scope[n] = "int"
                    progress = True
                    continue
                # 2) numerico / booleano / texto / una clase (o null)
                cx.scope[n] = "Number"
                ts = [("Number" if e[0] == "incdec" else self.stype(e, cx)) for e in es]
                cx.scope.pop(n, None)
                kinds = {t for t in ts if t != "null"}
                pick = None
                if ts and all(t in NUMERIC for t in ts):
                    pick = "Number"
                elif kinds == {"Boolean"} and "null" not in ts:
                    pick = "Boolean"
                elif kinds == {"String"}:
                    pick = "String"
                elif len(kinds) == 1:
                    t = next(iter(kinds))
                    if isinstance(t, str) and t in self.infos and not self.infos[t].iface and t not in ("Object",):
                        pick = t
                if pick:
                    types[n] = pick
                    cx.scope[n] = pick
                    progress = True
            if not progress:
                break
        for n in cand:  # limpiar el ambito provisional (el bucle de method_body lo rellena)
            cx.scope.pop(n, None)
        return [(n, types.get(n, t)) for n, t in hoisted]

    def method_body(self, head, params, rest, ret, body, static, pre):
        cx = Ctx({n: t for n, t, _ in params}, static)
        if rest:
            cx.scope[rest] = "Array"
        hoisted = self.hoist(body, cx.scope)
        hoisted = self.infer_locals(hoisted, body, cx)
        mfn = re.search(r"(\w+)::(\w+)\(", head)
        self.curfn = mfn.group(2) if mfn else "?"
        L = [head, "{"]
        if mfn:
            L.append(f'    AS3_FN("{mfn.group(1)}.{mfn.group(2)}");')
        for n, t in hoisted:
            cx.scope[n] = t
            L.append(f"    {self.ctype(t)} {mangle(n)} = {self.default(t) if t not in ('*', None) else 'as3::undefined'};")
        L += pre
        inner = self.st(body, cx, 0, ret_t=ret)
        L += inner[1:-1]
        if ret not in ("void", None):
            L.append(f"    return {self.default(ret) if ret not in ('*',) else 'as3::undefined'};")
        L.append("}")
        L.append("")
        return L


# Campos BitmapData de los que el objeto es el unico dueno (comprobado en el codigo de Flixel). Flash los liberaba con su
# recolector al sustituirlos; aqui, al reasignarlos, se libera el bitmap anterior (dispose) para no acumular memoria.
OWNED_BITMAP_FIELDS = {
    "FlxSprite": ("framePixels",),
    "FlxText": ("framePixels",),
    "FlxCamera": ("buffer", "_fill"),
}

def apply_owned_fields(cls, cpp):
    fields = OWNED_BITMAP_FIELDS.get(cls)
    if not fields:
        return cpp
    pat = re.compile(r"^(\s+)this->(" + "|".join(map(re.escape, fields)) + r") = (.*);$", re.M)
    return pat.sub(lambda m: f"{m.group(1)}flash::ownedSet(this->{m.group(2)}, {m.group(3)});", cpp)


def write_if_changed(path, text):
    try:
        with open(path, encoding="utf-8") as fh:
            if fh.read() == text:
                return
    except OSError:
        pass
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(text)


def main():
    out, files = sys.argv[1], sys.argv[2:]
    os.makedirs(out, exist_ok=True)
    parsed, fail = [], []
    flash_h = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "runtime", "flash.h")
    flash_names = set(parse_flash(flash_h))
    defined = set()
    for f in files:
        m_ = re.search(r"\b(?:class|interface)\s+(\w+)", open(f, encoding="utf-8").read())
        if m_ and "package mx" not in open(f, encoding="utf-8").read(400):
            defined.add(m_.group(1))
    collide = defined & flash_names  # nombres que existen en el juego y en flash.* (p. ej. Mouse)
    for f in files:
        src = open(f, encoding="utf-8").read()
        try:
            ren = {n for n in collide if not re.search(r"import\s+flash\.[\w.]*\b" + n + r"\s*;", src)}
            _, classes = Parser(lex(src), os.path.basename(f), ren, collide).file()
            parsed += classes
        except (SyntaxError, ValueError, IndexError, Unsupported) as ex:
            fail.append((os.path.basename(f), "análisis: " + str(ex)))
    parsed = [c for c in parsed if not c.get("pkg", "").startswith("mx")]  # Flex: sustituido por stubs en flash.h
    infos = {c["name"]: ClassInfo(c) for c in parsed}
    flash_h = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "runtime", "flash.h")
    flashinfo = {k: FlashInfo(k, v) for k, v in parse_flash(flash_h).items()}
    em = Emitter(infos, flashinfo)
    ok = []
    for c in parsed:
        try:
            em.infos = em.make_view(c.get("imports", []), c["name"])
            h, cpp = em.klass(c)
            n = mangle(c["name"])
            write_if_changed(os.path.join(out, n + ".h"), h)
            write_if_changed(os.path.join(out, n + ".cpp"), apply_owned_fields(n, cpp))
            ok.append(c["name"])
        except (TypeError, SyntaxError, Unsupported, KeyError, AttributeError, IndexError) as ex:
            import traceback
            where = traceback.extract_tb(ex.__traceback__)[-1]
            fail.append((c["name"], f"{type(ex).__name__}: {ex} [{where.name}:{where.lineno}]"))
    names = sorted(mangle(n) for n in infos)
    auto = sorted(n for n in em.stubs if re.fullmatch(r"[A-Z]\w*", n) and n not in flashinfo and n not in infos
                  and n not in AS3_BUILTIN and n not in PRIM and n not in STATIC_HOLDERS)
    with open(os.path.join(out, "flash_auto.h"), "w", encoding="utf-8") as fh:
        fh.write("// Generado: clases que el juego nombra y flash.h no define (assets embebidos, tipos de Flixel...).\n"
                 '#pragma once\n#include "as3rt.h"\nnamespace flash {\n' +
                 "".join((f"struct {n} : flash::Bitmap {{ template <class... A> void __construct(A...) {{}} }};\n" if n.startswith(("Img", "MissingImg"))
                         else f"struct {n} : as3::Object {{ template <class... A> void __construct(A...) {{}} }};\n") for n in auto) + "}\n")
    with open(os.path.join(out, "classes.h"), "w", encoding="utf-8") as fh:
        fh.write('#pragma once\n#include "as3rt.h"\n#include "flash_auto.h"\n' + "".join(f"struct {n};\n" for n in names))
    by_cpp = {mangle(n): n for n in infos}
    order, state = [], {}

    def visit(cn_):
        if state.get(cn_):
            return
        state[cn_] = 1
        for d in sorted(em.static_deps.get(by_cpp.get(cn_, ""), ())):
            if d in em.static_deps_mangled:
                visit(d)
        order.append(cn_)

    em.static_deps_mangled = {mangle(n): n for n in em.static_deps}
    em.static_deps = {k: {mangle(d) if d in infos else d for d in v} for k, v in em.static_deps.items()}
    em.static_deps = {k: {d for d in v} for k, v in em.static_deps.items()}
    for n in sorted(em.static_deps):
        visit(mangle(n))
    with open(os.path.join(out, "lines.txt"), "w", encoding="utf-8") as fh:
        for k, v in sorted(getattr(em, "line_map", {}).items()):
            fh.write(f"{k}\t{v}\n")
    with open(os.path.join(out, "statics.cpp"), "w", encoding="utf-8") as fh:
        fh.write('// Generado: inicializa los estaticos de todas las clases en orden de dependencias (AS3 los inicia al cargar la clase).\n'
                 '#include "all.h"\n#include <cstdio>\n\nvoid as3_init_statics()\n{\n')
        for cn_ in order:
            fh.write(f'    try {{ {cn_}::__init_statics(); }} catch (const as3::Thrown& t) {{ std::printf("[estaticos] {cn_}: excepcion AS3: %s\\n", as3::toString(t.value).str().c_str()); }}\n')
        fh.write("}\n\n// Generado: el recolector marca los estaticos de todas las clases (raices)\nvoid as3_trace_statics()\n{\n")
        for sf in getattr(em, "static_fields", []):
            fh.write(f"    as3::gc::mark({sf});\n")
        fh.write("}\n")
    with open(os.path.join(out, "all.h"), "w", encoding="utf-8") as fh:
        fh.write("#pragma once\n" + "".join(f'#include "{mangle(n)}.h"\n' for n in ok))
    with open(os.path.join(out, "embeds.txt"), "w", encoding="utf-8") as fh:
        for c in parsed:
            if c.get("embed"):
                fh.write(f"{c['name']}\t{c['embed'].get('source', '')}\t{c['embed'].get('mimeType', '')}\n")
    with open(os.path.join(out, "stubs.txt"), "w", encoding="utf-8") as fh:
        fh.write("\n".join(sorted(em.stubs)) + "\n")
    print(f"traducidas {len(ok)} de {len(ok) + len(fail)}")
    for n, why in fail:
        print(f"FALLO {n}: {why}")
    sys.exit(1 if fail else 0)


if __name__ == "__main__":
    main()
