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

    def leer(self, limite):
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

    def limpiar(self):
        fin = time.time() + 1.0
        while time.time() < fin:
            r, _, _ = select.select([self.p.stdout], [], [], 0.1)
            if not r:
                break
            trozo = os.read(self.p.stdout.fileno(), 65536)
            if not trozo:
                break
            self.buf += trozo

def main():
    s = Lsp()
    s.enviar({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}})
    r = s.leer(10)
    caps = r.get("result", {}).get("capabilities", {})
    if caps.get("textDocumentSync") == 1 and caps.get("documentFormattingProvider") is True:
        ok("initialize devuelve capacidades")
    else:
        fallo("initialize", json.dumps(r)[:200])

    uri = "file:///tmp/sylt_lsp_mal.sy"
    s.enviar({"jsonrpc": "2.0", "method": "textDocument/didOpen", "params": {
        "textDocument": {"uri": uri, "languageId": "sylt", "version": 1,
                         "text": "fn Main():\n    Print(X)\n"}}})
    r = s.leer(10)
    diags = r.get("params", {}).get("diagnostics", [])
    if (r.get("method") == "textDocument/publishDiagnostics" and len(diags) == 1
            and diags[0]["code"] == "E0301" and diags[0]["severity"] == 1
            and diags[0]["range"]["start"]["line"] == 1
            and diags[0]["source"] == "sylt"):
        ok("didOpen publica el error de variable no definida con posición")
    else:
        fallo("didOpen", json.dumps(r)[:300])

    s.enviar({"jsonrpc": "2.0", "method": "textDocument/didChange", "params": {
        "textDocument": {"uri": uri, "version": 2},
        "contentChanges": [{"text": "fn Main():\n    let X = 1\n    Print(X)\n"}]}})
    r = s.leer(10)
    if r.get("params", {}).get("diagnostics") == []:
        ok("didChange limpia los diagnósticos al arreglar el código")
    else:
        fallo("didChange", json.dumps(r)[:300])

    uri2 = "file:///tmp/sylt_lsp_orden.sy"
    sucio = "fn Main():\n    let X=-1+2\n    Print( X )\n"
    s.enviar({"jsonrpc": "2.0", "method": "textDocument/didOpen", "params": {
        "textDocument": {"uri": uri2, "languageId": "sylt", "version": 1,
                         "text": sucio}}})
    r = s.leer(10)
    s.enviar({"jsonrpc": "2.0", "id": 2, "method": "textDocument/formatting",
              "params": {"textDocument": {"uri": uri2}, "options": {}}})
    r = s.leer(10)
    edits = r.get("result", [])
    if len(edits) == 1 and "let X = -1 + 2" in edits[0]["newText"] \
            and "Print(X)" in edits[0]["newText"]:
        ok("formatting devuelve una edición con el texto canónico")
    else:
        fallo("formatting", json.dumps(r)[:300])

    s.enviar({"jsonrpc": "2.0", "id": 3, "method": "metodo/que/no/existe", "params": {}})
    r = s.leer(10)
    if r.get("error", {}).get("code") == -32601:
        ok("método desconocido responde -32601")
    else:
        fallo("método desconocido", json.dumps(r)[:200])

    s.enviar({"jsonrpc": "2.0", "id": 4, "method": "shutdown", "params": None})
    r = s.leer(10)
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
