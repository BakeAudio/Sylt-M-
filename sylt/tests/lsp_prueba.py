import json
import os
import select
import subprocess
import sys
import time

BIEN = 0
MAL = 0

def ok(nombre):
    global BIEN
    BIEN += 1
    print(f"  ok: {nombre}")

def fallo(nombre, detalle=""):
    global MAL
    MAL += 1
    print(f"  FALLO: {nombre} {detalle}")

class Lsp:
    def __init__(self):
        self.p = subprocess.Popen(
            ["./syltc", "lsp"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
        )
        self.buf = b""

    def leer_bruto(self, limite):
        fin = time.time() + limite
        while True:
            idx = self.buf.find(b"\r\n\r\n")
            if idx >= 0:
                cab = self.buf[:idx].decode("utf-8", "replace")
                resto = self.buf[idx + 4:]
                largo = 0
                for linea in cab.split("\r\n"):
                    if linea.lower().startswith("content-length:"):
                        largo = int(linea.split(":", 1)[1].strip())
                while len(resto) < largo:
                    if time.time() > fin:
                        raise TimeoutError("sin respuesta del servidor")
                    r, _, _ = select.select([self.p.stdout], [], [], 0.2)
                    if not r:
                        continue
                    trozo = os.read(self.p.stdout.fileno(), 65536)
                    if not trozo:
                        raise RuntimeError("el servidor cerró stdout")
                    resto += trozo
                cuerpo = resto[:largo]
                self.buf = resto[largo:]
                return json.loads(cuerpo.decode("utf-8"))
            if time.time() > fin:
                raise TimeoutError("sin cabeceras del servidor")
            r, _, _ = select.select([self.p.stdout], [], [], 0.2)
            if not r:
                if self.p.poll() is not None:
                    raise RuntimeError("el servidor terminó de forma inesperada")
                continue
            trozo = os.read(self.p.stdout.fileno(), 65536)
            if not trozo:
                raise RuntimeError("el servidor cerró stdout")
            self.buf += trozo

    def enviar(self, obj):
        cuerpo = json.dumps(obj).encode("utf-8")
        self.p.stdin.write(b"Content-Length: " + str(len(cuerpo)).encode() + b"\r\n\r\n" + cuerpo)
        self.p.stdin.flush()

    def esperar_diags(self, uri, limite=10):
        fin = time.time() + limite
        while time.time() < fin:
            r = self.leer_bruto(max(0.2, fin - time.time()))
            if r.get("method") == "textDocument/publishDiagnostics" and \
                    r.get("params", {}).get("uri") == uri:
                return r["params"]["diagnostics"]
        raise TimeoutError(f"sin diagnósticos para {uri}")

    def esperar_diags_varios(self, uris, limite=10):
        pend = {u: None for u in uris}
        fin = time.time() + limite
        while time.time() < fin and any(v is None for v in pend.values()):
            r = self.leer_bruto(max(0.2, fin - time.time()))
            if r.get("method") == "textDocument/publishDiagnostics":
                u = r.get("params", {}).get("uri")
                if u in pend and pend[u] is None:
                    pend[u] = r["params"]["diagnostics"]
        return pend

    def esperar_respuesta(self, id_obj, limite=10):
        fin = time.time() + limite
        while time.time() < fin:
            r = self.leer_bruto(max(0.2, fin - time.time()))
            if r.get("id") == id_obj and ("result" in r or "error" in r):
                return r
        raise TimeoutError(f"sin respuesta para el id {id_obj}")

def main():
    s = Lsp()
    s.enviar({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}})
    r = s.esperar_respuesta(1)
    caps = r.get("result", {}).get("capabilities", {})
    version = r.get("result", {}).get("serverInfo", {}).get("version", "")
    if (caps.get("textDocumentSync") == 1 and caps.get("documentFormattingProvider") is True
            and caps.get("hoverProvider") is True
            and isinstance(caps.get("completionProvider"), dict)
            and caps.get("positionEncoding") == "utf-16" and version == "0.7.3"):
        ok("initialize anuncia hover, completion, utf-16 y la versión 0.7.3")
    else:
        fallo("initialize", json.dumps(r)[:300])

    uri = "file:///tmp/sylt_lsp_mal.sy"
    s.enviar({"jsonrpc": "2.0", "method": "textDocument/didOpen", "params": {
        "textDocument": {"uri": uri, "languageId": "sylt", "version": 1,
                         "text": "fn Main():\n    Print(X)\n"}}})
    diags = s.esperar_diags(uri)
    if (len(diags) == 1 and diags[0]["code"] == "E0301" and diags[0]["severity"] == 1
            and diags[0]["range"]["start"]["line"] == 1
            and diags[0]["source"] == "sylt"):
        ok("didOpen publica el error de variable no definida con posición")
    else:
        fallo("didOpen", json.dumps(diags)[:300])

    s.enviar({"jsonrpc": "2.0", "method": "textDocument/didChange", "params": {
        "textDocument": {"uri": uri, "version": 2},
        "contentChanges": [{"text": "fn Main():\n    let X = 1\n    Print(X)\n"}]}})
    diags = s.esperar_diags(uri)
    if diags == []:
        ok("didChange limpia los diagnósticos al arreglar el código")
    else:
        fallo("didChange", json.dumps(diags)[:300])

    uri_cols = "file:///tmp/sylt_lsp_cols.sy"
    s.enviar({"jsonrpc": "2.0", "method": "textDocument/didOpen", "params": {
        "textDocument": {"uri": uri_cols, "languageId": "sylt", "version": 1,
                         "text": 'fn Foo(X: int, Y: int):\n    Print(X + Y)\n\nfn Main():\n    Foo("ñññ", "a")\n'}}})
    diags = s.esperar_diags(uri_cols)
    segundo = [d for d in diags if d["range"]["start"]["character"] > 10]
    if (len(diags) == 2 and len(segundo) == 1
            and segundo[0]["range"]["start"] == {"line": 4, "character": 15}
            and segundo[0]["range"]["end"] == {"line": 4, "character": 18}):
        ok("las columnas del LSP van en unidades UTF-16 (15..18, no 18..21)")
    else:
        fallo("columnas utf-16", json.dumps(diags)[:300])

    os.makedirs("/tmp/sylt_lsp_mod", exist_ok=True)
    open("/tmp/sylt_lsp_mod/ayuda.sy", "w").write("fn Mal():\n    Print(NoExiste)\n")
    uri_mod = "file:///tmp/sylt_lsp_mod/ayuda.sy"
    uri_prin = "file:///tmp/sylt_lsp_mod/principal.sy"
    s.enviar({"jsonrpc": "2.0", "method": "textDocument/didOpen", "params": {
        "textDocument": {"uri": uri_prin, "languageId": "sylt", "version": 1,
                         "text": "import ayuda\n\nfn Main():\n    Print(ayuda.Mal())\n"}}})
    recibidos = s.esperar_diags_varios([uri_prin, uri_mod])
    diags_prin = recibidos[uri_prin]
    diags_mod = recibidos[uri_mod]
    if diags_prin == [] and len(diags_mod) == 1 and diags_mod[0]["code"] == "E0301":
        ok("el error de un módulo importado se publica en su propio fichero")
    else:
        fallo("diagnósticos de módulos",
              f"principal={json.dumps(diags_prin)[:120]} módulo={json.dumps(diags_mod)[:200]}")

    uri_doc = "file:///tmp/sylt_lsp_doc.sy"
    s.enviar({"jsonrpc": "2.0", "method": "textDocument/didOpen", "params": {
        "textDocument": {"uri": uri_doc, "languageId": "sylt", "version": 1,
                         "text": "type Punto:\n    X: int\n    Y: int\n\nfn Main():\n    Print(1)\n"}}})
    s.esperar_diags(uri_doc)
    s.enviar({"jsonrpc": "2.0", "id": 7, "method": "textDocument/hover", "params": {
        "textDocument": {"uri": uri_doc},
        "position": {"line": 4, "character": 6}}})
    r = s.esperar_respuesta(7)
    hover_fn = (r.get("result", {}) or {}).get("contents", {}).get("value", "")
    s.enviar({"jsonrpc": "2.0", "id": 8, "method": "textDocument/hover", "params": {
        "textDocument": {"uri": uri_doc},
        "position": {"line": 0, "character": 6}}})
    r = s.esperar_respuesta(8)
    hover_ty = (r.get("result", {}) or {}).get("contents", {}).get("value", "")
    s.enviar({"jsonrpc": "2.0", "id": 9, "method": "textDocument/hover", "params": {
        "textDocument": {"uri": uri_doc},
        "position": {"line": 0, "character": 2}}})
    r = s.esperar_respuesta(9)
    hover_kw = (r.get("result", {}) or {}).get("contents", {}).get("value", "")
    if "fn Main()" in hover_fn and "type Punto" in hover_ty and "X: int" in hover_ty \
            and "`type`" in hover_kw:
        ok("hover documenta la función, el tipo con sus campos y las palabras clave")
    else:
        fallo("hover", f"fn='{hover_fn[:60]}' ty='{hover_ty[:80]}' kw='{hover_kw[:60]}'")

    s.enviar({"jsonrpc": "2.0", "id": 10, "method": "textDocument/completion", "params": {
        "textDocument": {"uri": uri_doc},
        "position": {"line": 5, "character": 10}}})
    r = s.esperar_respuesta(10)
    etiquetas = [i["label"] for i in (r.get("result", []) or [])]
    s.enviar({"jsonrpc": "2.0", "id": 11, "method": "textDocument/completion", "params": {
        "textDocument": {"uri": uri_prin},
        "position": {"line": 3, "character": 16}}})
    r = s.esperar_respuesta(11)
    miembros = [i["label"] for i in (r.get("result", []) or [])]
    if ("Print" in etiquetas and "Punto" in etiquetas and "while" in etiquetas
            and "Mal" in miembros):
        ok("completion lista el entorno global y los miembros del módulo tras el punto")
    else:
        fallo("completion", f"global={etiquetas[:8]} miembros={miembros[:8]}")

    uri2 = "file:///tmp/sylt_lsp_orden.sy"
    sucio = "fn Main():\n    let X=-1+2\n    Print( X )\n"
    s.enviar({"jsonrpc": "2.0", "method": "textDocument/didOpen", "params": {
        "textDocument": {"uri": uri2, "languageId": "sylt", "version": 1,
                         "text": sucio}}})
    s.esperar_diags(uri2)
    s.enviar({"jsonrpc": "2.0", "id": 2, "method": "textDocument/formatting",
              "params": {"textDocument": {"uri": uri2}, "options": {}}})
    r = s.esperar_respuesta(2)
    edits = r.get("result", [])
    if len(edits) == 1 and "let X = -1 + 2" in edits[0]["newText"] \
            and "Print(X)" in edits[0]["newText"]:
        ok("formatting devuelve una edición con el texto canónico")
    else:
        fallo("formatting", json.dumps(r)[:300])

    s.enviar({"jsonrpc": "2.0", "id": 3, "method": "metodo/que/no/existe", "params": {}})
    r = s.esperar_respuesta(3)
    if r.get("error", {}).get("code") == -32601:
        ok("método desconocido responde -32601")
    else:
        fallo("método desconocido", json.dumps(r)[:200])

    s.p.stdin.write(b"Content-Length: 0\r\n\r\n")
    s.p.stdin.flush()
    r = s.leer_bruto(10)
    if r.get("error", {}).get("code") == -32600 and s.p.poll() is None:
        ok("un frame con Content-Length 0 responde error y no mata el servidor")
    else:
        fallo("frame defectuoso", json.dumps(r)[:200])

    s.p.stdin.write(b"X-Frame-Rota: vaya\r\n\r\n{\"jsonrpc\": \"2.0\"}")
    s.p.stdin.flush()
    r = s.leer_bruto(10)
    if r.get("error", {}).get("code") == -32600 and s.p.poll() is None:
        ok("un frame sin Content-Length responde error y el servidor sigue")
    else:
        fallo("frame sin Content-Length", json.dumps(r)[:200])

    s.enviar({"jsonrpc": "2.0", "method": "textDocument/didChange", "params": {
        "textDocument": {"uri": uri, "version": 3},
        "contentChanges": [{"text": "fn Main():\n    Print(X)\n"}]}})
    diags = s.esperar_diags(uri)
    if len(diags) == 1 and diags[0]["code"] == "E0301":
        ok("el servidor sigue analizando con normalidad tras los frames defectuosos")
    else:
        fallo("análisis tras frames defectuosos", json.dumps(diags)[:200])

    s.enviar({"jsonrpc": "2.0", "method": "textDocument/didClose", "params": {
        "textDocument": {"uri": uri}}})
    diags = s.esperar_diags(uri)
    if diags == []:
        ok("didClose limpia los diagnósticos del documento cerrado")
    else:
        fallo("didClose", json.dumps(diags)[:200])

    uri_roto = "file:///tmp/sylt_lsp_roto.sy"
    uri_interp = "file:///tmp/sylt_lsp_interp.sy"
    uri_inst = "file:///tmp/sylt_lsp_inst.sy"
    servidor_muerto = False
    try:
        s.enviar({"jsonrpc": "2.0", "method": "textDocument/didOpen", "params": {
            "textDocument": {"uri": uri_roto, "languageId": "sylt", "version": 1,
                             "text": "fn Main():\n    let x = 1 +\n"}}})
        diags_roto = s.esperar_diags(uri_roto)
        if len(diags_roto) == 1 and diags_roto[0]["code"] == "E0201":
            ok("didOpen con sintaxis rota publica el diagnóstico y no mata el servidor")
        else:
            fallo("sintaxis rota", json.dumps(diags_roto)[:200])

        s.enviar({"jsonrpc": "2.0", "id": 12, "method": "textDocument/hover", "params": {
            "textDocument": {"uri": uri_roto},
            "position": {"line": 1, "character": 8}}})
        r = s.esperar_respuesta(12)
        if "result" in r:
            ok("hover sobre código roto responde sin tirar el servidor")
        else:
            fallo("hover sobre código roto", json.dumps(r)[:200])

        s.enviar({"jsonrpc": "2.0", "id": 13, "method": "textDocument/completion",
                  "params": {"textDocument": {"uri": uri_roto},
                             "position": {"line": 1, "character": 15}}})
        r = s.esperar_respuesta(13)
        if isinstance(r.get("result"), list):
            ok("completion sobre código roto responde sin tirar el servidor")
        else:
            fallo("completion sobre código roto", json.dumps(r)[:200])

        s.enviar({"jsonrpc": "2.0", "method": "textDocument/didChange", "params": {
            "textDocument": {"uri": uri_roto, "version": 2},
            "contentChanges": [{"text": "fn Main():\n    let x = 1\n    Print(x)\n"}]}})
        diags = s.esperar_diags(uri_roto)
        if diags == []:
            ok("al arreglar el código el análisis vuelve a estar limpio")
        else:
            fallo("recuperación tras sintaxis rota", json.dumps(diags)[:200])

        s.enviar({"jsonrpc": "2.0", "method": "textDocument/didOpen", "params": {
            "textDocument": {"uri": uri_interp, "languageId": "sylt", "version": 1,
                             "text": 'fn Main():\n    Print("{C.\n'}}})
        diags_interp = s.esperar_diags(uri_interp)
        codigos = {d["code"] for d in diags_interp}
        if "E0102" in codigos:
            ok("la interpolación sin cerrar publica su error y el servidor sigue vivo")
        else:
            fallo("interpolación sin cerrar", json.dumps(diags_interp)[:200])

        s.enviar({"jsonrpc": "2.0", "id": 14, "method": "textDocument/hover", "params": {
            "textDocument": {"uri": uri_interp},
            "position": {"line": 1, "character": 11}}})
        s.esperar_respuesta(14)
        s.enviar({"jsonrpc": "2.0", "id": 15, "method": "textDocument/completion",
                  "params": {"textDocument": {"uri": uri_interp},
                             "position": {"line": 1, "character": 15}}})
        s.esperar_respuesta(15)
        ok("hover y completion sobre la interpolación rota responden")

        s.enviar({"jsonrpc": "2.0", "method": "textDocument/didOpen", "params": {
            "textDocument": {"uri": uri_inst, "languageId": "sylt", "version": 1,
                             "text": "type Punto:\n    X: int\n\nfn Main():\n"
                                     "    let C = Punto(X: 1)\n    let Y = C.X\n"}}})
        s.esperar_diags(uri_inst)
        s.enviar({"jsonrpc": "2.0", "id": 16, "method": "textDocument/completion",
                  "params": {"textDocument": {"uri": uri_inst},
                             "position": {"line": 5, "character": 14}}})
        r = s.esperar_respuesta(16)
        if isinstance(r.get("result"), list):
            ok("completion tras `C.` (base que no es módulo) responde una lista")
        else:
            fallo("completion tras punto no módulo", json.dumps(r)[:200])
    except Exception as E:
        servidor_muerto = s.p.poll() is not None
        fallo("código roto en el editor",
              f"el servidor murió o dejó de responder: {type(E).__name__}: {E}")

    if servidor_muerto:
        fallo("shutdown y exit", "omitidos: el servidor no sobrevivió al código roto")
        print(f"lsp: {BIEN} comprobaciones superadas, {MAL} fallos")
        return 1

    s.enviar({"jsonrpc": "2.0", "id": 4, "method": "shutdown", "params": None})
    r = s.esperar_respuesta(4)
    if "result" in r and r["result"] is None:
        ok("shutdown responde null")
    else:
        fallo("shutdown", json.dumps(r)[:200])

    s.enviar({"jsonrpc": "2.0", "method": "exit", "params": None})
    codigo = s.p.wait(timeout=10)
    if codigo == 0:
        ok("exit sale con 0 tras shutdown")
    else:
        fallo("exit", f"código {codigo}")

    print(f"lsp: {BIEN} comprobaciones superadas, {MAL} fallos")
    return 1 if MAL else 0

if __name__ == "__main__":
    sys.exit(main())
