"""flashtypes.py — lee runtime/flash.h y devuelve, para cada clase de `flash::`, su base y sus miembros con tipos AS3.

El traductor lo usa para tipar `x`, `width`, `addChild(...)`, `Event.ENTER_FRAME`... sin duplicar la API en dos sitios:
la única fuente de verdad es flash.h.
"""
import re


def cpp2as3(t):
    t = re.sub(r"\b(const|static|inline|virtual|struct|constexpr)\b", " ", t)
    t = t.replace("&", " ").strip()
    t = re.sub(r"\s+", " ", t)
    base = {"double": "Number", "int32_t": "int", "int": "int", "uint32_t": "uint", "unsigned": "uint", "bool": "Boolean",
            "void": "void", "size_t": "uint"}
    if t in base:
        return base[t]
    t2 = t.replace("as3::", "").replace("flash::", "")
    if t2 == "String":
        return "String"
    if t2 == "Function":
        return "Function"
    if t2 == "Any":
        return "*"
    if t2.endswith("*"):
        n = t2[:-1].strip()
        if re.fullmatch(r"\w+", n):
            return n
    return "*"


def _strip_comments(s):
    s = re.sub(r"//[^\n]*", "", s)
    return re.sub(r"/\*.*?\*/", "", s, flags=re.S)


def _split_top(s, sep=","):
    out, depth, cur = [], 0, ""
    for ch in s:
        if ch in "(<{[":
            depth += 1
        elif ch in ")>}]":
            depth -= 1
        if ch == sep and depth == 0:
            out.append(cur); cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur)
    return out


def _statements(body):
    """Trocea el cuerpo de una struct en declaraciones de nivel 0 (campos y cabeceras de método)."""
    stmts, depth, cur, i = [], 0, "", 0
    while i < len(body):
        ch = body[i]
        if ch == "{" and depth == 0:
            head = cur.strip()
            # llave de inicialización de campo (`= {}`)  vs. cuerpo de método
            if head.endswith("=") or re.search(r"=\s*$", head):
                cur += ch; depth += 1
            else:
                stmts.append(head); cur = ""; depth += 1
        elif ch == "{":
            depth += 1
            if depth > 1 and cur:
                pass
        elif ch == "}":
            depth -= 1
            if depth == 0 and not cur.strip().startswith("="):
                cur = cur if cur.strip().startswith("=") else ""
        elif ch == ";" and depth == 0:
            stmts.append(cur.strip()); cur = ""
        elif depth == 0:
            cur += ch
        i += 1
    return [s for s in stmts if s]


def parse(path):
    src = _strip_comments(open(path, encoding="utf-8").read())
    out = {}
    for m in re.finditer(r"\bstruct\s+(\w+)\s*(?::\s*(?:public\s+)?([\w:]+))?\s*\{", src):
        name, base = m.group(1), m.group(2)
        # cuerpo por emparejamiento de llaves
        i, depth = m.end(), 1
        while depth:
            depth += {"{": 1, "}": -1}.get(src[i], 0)
            i += 1
        body = src[m.end():i - 1]
        if name in out:
            continue
        base = base.split("::")[-1] if base else None
        info = {"base": base, "fields": {}, "sfields": {}, "methods": {}, "smethods": {}, "getters": {}, "setters": {}}
        for cm in re.finditer(r"FLASH_CONST\((\w+),", body):
            info["sfields"][cm.group(1)] = "String"
        body = re.sub(r"FLASH_CONST\([^;]*\);", "", body)
        for st in _statements(body):
            st = st.strip()
            if not st or st.startswith(("using ", "template", "typedef", "enum", "friend", "#")):
                continue
            st = re.sub(r"^template\s*<[^>]*>\s*", "", st)
            paren = st.find("(")
            eq = st.find("=")
            if paren >= 0 and (eq < 0 or paren < eq):
                mm = re.match(r"^(?P<mods>(?:(?:static|virtual|inline|explicit|constexpr)\s+)*)(?P<ret>.*?)\b(?P<n>[\w~]+)\s*\((?P<p>.*)\)\s*(?:const)?\s*(?:override)?\s*$", st, re.S)
                if not mm:
                    continue
                n = mm.group("n")
                if n in ("__construct", name) or n.startswith("~") or n.startswith("__"):
                    continue
                ret = cpp2as3(mm.group("ret")) if mm.group("ret").strip() else "void"
                params = []
                for p in _split_top(mm.group("p")):
                    p = p.strip()
                    if not p or p == "...":
                        continue
                    p = p.split("=")[0].strip()
                    pm = re.match(r"^(.*?)(\w+)?$", p)
                    ty = cpp2as3(pm.group(1)) if pm and pm.group(1).strip() else cpp2as3(p)
                    params.append((pm.group(2) or "p", ty, "d" if "=" in mm.group("p") else None))
                static = "static" in mm.group("mods")
                if n.startswith("get_"):
                    info["getters"][n[4:]] = (ret, static)
                elif n.startswith("set_"):
                    info["setters"][n[4:]] = (params[0][1] if params else "*", static)
                else:
                    (info["smethods"] if static else info["methods"])[n] = (ret, params)
            else:
                static = bool(re.match(r"^\s*static\b", st))
                decl = re.sub(r"^\s*(static\s+inline\s+|static\s+|inline\s+)", "", st)
                decl = re.sub(r"^\s*const\s+", "", decl)
                tm = re.match(r"^((?:[\w:]+(?:<[^>]*>)?[\s\*]*?)+?)\s*(\*?\s*\w+\s*(?:=.*)?)$", decl.split(",")[0].strip(), re.S)
                if not tm:
                    continue
                first = decl.split(",")[0].strip()
                mm = re.match(r"^(.*?[\s\*])(\w+)\s*(?:=.*)?$", first, re.S)
                if not mm:
                    continue
                ty = cpp2as3(mm.group(1))
                names = [mm.group(2)]
                for rest in _split_top(decl)[1:]:
                    nm = re.match(r"^\s*\*?\s*(\w+)", rest)
                    if nm:
                        names.append(nm.group(1))
                for n in names:
                    (info["sfields"] if static else info["fields"])[n] = ty
        out[name] = info
    return out


if __name__ == "__main__":
    import sys, json
    d = parse(sys.argv[1])
    for k in sys.argv[2:]:
        print(k, json.dumps(d.get(k), ensure_ascii=False, indent=1))
    print(len(d), "clases")
