#include "fmt.hpp"
#include "json.hpp"
#include "loader.hpp"
#include "check.hpp"
#include <cstdlib>
#include <iostream>
#include <map>
#include <unordered_map>
namespace sylt {
namespace {

std::string DecodificarUri(const std::string& uri) {
    std::string p = uri;
    if (StartsWith(p, "file://")) p = p.substr(7);
    std::string out;
    auto EsHex = [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    };
    auto ValorHex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return c - 'A' + 10;
    };
    for (size_t i = 0; i < p.size(); i++) {
        if (p[i] == '%' && i + 2 < p.size() && EsHex(p[i + 1]) && EsHex(p[i + 2])) {
            out.push_back((char)(ValorHex(p[i + 1]) * 16 + ValorHex(p[i + 2])));
            i += 2;
        } else {
            out.push_back(p[i]);
        }
    }
    return out;
}

JsonValue Posicion(int linea0, int col0) {
    JsonValue p = JsonValue::MakeObj();
    p.Set("line", JsonValue::MakeInt(linea0));
    p.Set("character", JsonValue::MakeInt(col0));
    return p;
}

JsonValue PosicionFinal(const std::string& text) {
    if (text.empty()) return Posicion(0, 0);
    int lineas = 0;
    size_t ultima = 0;
    for (char c : text) {
        if (c == '\n') {
            lineas++;
            ultima = 0;
        } else {
            ultima++;
        }
    }
    if (lineas == 0) return Posicion(0, (int)text.size());
    return Posicion(lineas, (int)ultima);
}

JsonValue DiagsDeBuffer(const std::string& path, const std::string& text,
                        std::unordered_map<std::string, std::string>& overlays) {
    overlays[path] = text;
    SourceFile src;
    src.Load(path, text);
    Reporter rep(src, false);
    std::vector<Diag> buf;
    rep.SetSink(&buf);
    Loader ld(rep, ResolveLibDir(""));
    ld.PkgDir = ResolvePkgDir("");
    ld.Overlays = &overlays;
    if (ld.Load(path, "__main__", false)) {
        ProgramP prog = ld.Merge();
        if (rep.ErrorCount() == 0) {
            std::unordered_map<std::string, const SourceFile*> files;
            for (auto& U : ld.Units)
                files[U.Name == "__main__" ? "" : U.Name] = &U.Src;
            Check(prog, src, rep, &files, false);
        }
    }
    overlays.erase(path);
    JsonValue arr = JsonValue::MakeArr();
    for (const Diag& d : buf) {
        const std::string& nombre = d.File ? d.File->Name : path;
        if (nombre != path) continue;
        int ln = d.PrimaryValid ? d.Primary.Line : 1;
        int col = d.PrimaryValid ? d.Primary.Col : 1;
        int len = (d.PrimaryValid && d.Primary.Len > 0) ? d.Primary.Len : 1;
        if (ln < 1) ln = 1;
        if (col < 1) col = 1;
        JsonValue rango = JsonValue::MakeObj();
        rango.Set("start", Posicion(ln - 1, col - 1));
        rango.Set("end", Posicion(ln - 1, col - 1 + len));
        JsonValue dg = JsonValue::MakeObj();
        dg.Set("range", rango);
        dg.Set("severity", JsonValue::MakeInt(d.Severity == Diag::Sev::Error ? 1 : 2));
        dg.Set("code", JsonValue::MakeText(d.Code));
        dg.Set("source", JsonValue::MakeText("sylt"));
        std::string msg = d.Msg;
        for (const auto& n : d.Notes) msg += "\nnota: " + n.Text;
        for (const auto& h : d.Helps) msg += "\nayuda: " + h;
        dg.Set("message", JsonValue::MakeText(msg));
        arr.Push(dg);
    }
    return arr;
}

struct ServidorLsp {
    std::unordered_map<std::string, std::string> Documentos;
    std::unordered_map<std::string, std::string> Overlays;
    bool ApagadoRecibido = false;

    void Enviar(const std::string& cuerpo) {
        std::cout << "Content-Length: " << cuerpo.size() << "\r\n\r\n" << cuerpo;
        std::cout.flush();
    }
    void EnviarJson(const JsonValue& v) { Enviar(JsonDump(v)); }
    void Responder(const JsonValue& id, JsonValue resultado) {
        JsonValue r = JsonValue::MakeObj();
        r.Set("jsonrpc", JsonValue::MakeText("2.0"));
        r.Set("id", id);
        r.Set("result", std::move(resultado));
        EnviarJson(r);
    }
    void ResponderError(const JsonValue& id, int codigo, const std::string& msg) {
        JsonValue e = JsonValue::MakeObj();
        e.Set("code", JsonValue::MakeInt(codigo));
        e.Set("message", JsonValue::MakeText(msg));
        JsonValue r = JsonValue::MakeObj();
        r.Set("jsonrpc", JsonValue::MakeText("2.0"));
        r.Set("id", id);
        r.Set("error", e);
        EnviarJson(r);
    }
    void Notificar(const std::string& metodo, const JsonValue& params) {
        JsonValue r = JsonValue::MakeObj();
        r.Set("jsonrpc", JsonValue::MakeText("2.0"));
        r.Set("method", JsonValue::MakeText(metodo));
        r.Set("params", params);
        EnviarJson(r);
    }
    void PublicarDiags(const std::string& uri, JsonValue diags) {
        JsonValue params = JsonValue::MakeObj();
        params.Set("uri", JsonValue::MakeText(uri));
        params.Set("diagnostics", std::move(diags));
        Notificar("textDocument/publishDiagnostics", params);
    }
    void Diagnostizar(const std::string& uri, const std::string& text) {
        std::string path = DecodificarUri(uri);
        PublicarDiags(uri, DiagsDeBuffer(path, text, Overlays));
    }
    std::string TextoDe(const std::string& uri) {
        auto it = Documentos.find(uri);
        if (it != Documentos.end()) return it->second;
        std::string text;
        ReadFile(DecodificarUri(uri), text);
        return text;
    }
    void Abrir(const JsonValue& params) {
        const JsonValue* td = params.At("textDocument");
        if (!td) return;
        const JsonValue* uri = td->At("uri");
        const JsonValue* text = td->At("text");
        if (!uri || !text || !uri->IsText()) return;
        Documentos[uri->AsText()] = text->AsText();
        Diagnostizar(uri->AsText(), text->AsText());
    }
    void Cambiar(const JsonValue& params) {
        const JsonValue* td = params.At("textDocument");
        if (!td) return;
        const JsonValue* uri = td->At("uri");
        if (!uri || !uri->IsText()) return;
        const JsonValue* cambios = params.At("contentChanges");
        if (!cambios || !cambios->IsArr() || cambios->A.empty()) return;
        const JsonValue* ultimo = &cambios->A.back();
        const JsonValue* text = ultimo->At("text");
        if (!text || !text->IsText()) return;
        Documentos[uri->AsText()] = text->AsText();
        Diagnostizar(uri->AsText(), text->AsText());
    }
    void Cerrar(const JsonValue& params) {
        const JsonValue* td = params.At("textDocument");
        if (!td) return;
        const JsonValue* uri = td->At("uri");
        if (!uri || !uri->IsText()) return;
        Documentos.erase(uri->AsText());
        JsonValue vacio = JsonValue::MakeArr();
        PublicarDiags(uri->AsText(), std::move(vacio));
    }
    void Formatear(const JsonValue& id, const JsonValue& params) {
        const JsonValue* td = params.At("textDocument");
        if (!td) {
            ResponderError(id, -32602, "falta textDocument");
            return;
        }
        const JsonValue* uri = td->At("uri");
        if (!uri || !uri->IsText()) {
            ResponderError(id, -32602, "falta uri");
            return;
        }
        std::string text = TextoDe(uri->AsText());
        std::string out;
        std::vector<Diag> buf;
        JsonValue edits = JsonValue::MakeArr();
        if (FormatSource(DecodificarUri(uri->AsText()), text, out, &buf) && out != text) {
            JsonValue edit = JsonValue::MakeObj();
            JsonValue rango = JsonValue::MakeObj();
            rango.Set("start", Posicion(0, 0));
            rango.Set("end", PosicionFinal(text));
            edit.Set("range", rango);
            edit.Set("newText", JsonValue::MakeText(out));
            edits.Push(edit);
        }
        Responder(id, std::move(edits));
    }
    void Despachar(const JsonValue& msg) {
        const JsonValue* metodo = msg.At("method");
        if (!metodo || !metodo->IsText()) return;
        const JsonValue* idv = msg.At("id");
        const JsonValue* params = msg.At("params");
        JsonValue vacio = JsonValue::MakeObj();
        const JsonValue& P = params ? *params : vacio;
        const std::string& m = metodo->AsText();
        if (m == "initialize") {
            JsonValue caps = JsonValue::MakeObj();
            caps.Set("textDocumentSync", JsonValue::MakeInt(1));
            caps.Set("documentFormattingProvider", JsonValue::MakeBool(true));
            JsonValue info = JsonValue::MakeObj();
            info.Set("name", JsonValue::MakeText("sylt"));
            info.Set("version", JsonValue::MakeText("0.6.0"));
            JsonValue res = JsonValue::MakeObj();
            res.Set("capabilities", caps);
            res.Set("serverInfo", info);
            Responder(idv ? *idv : JsonValue::MakeNull(), std::move(res));
            return;
        }
        if (m == "initialized") return;
        if (m == "shutdown") {
            ApagadoRecibido = true;
            Responder(idv ? *idv : JsonValue::MakeNull(), JsonValue::MakeNull());
            return;
        }
        if (m == "exit") std::exit(ApagadoRecibido ? 0 : 1);
        if (m == "textDocument/didOpen") {
            Abrir(P);
            return;
        }
        if (m == "textDocument/didChange") {
            Cambiar(P);
            return;
        }
        if (m == "textDocument/didClose") {
            Cerrar(P);
            return;
        }
        if (m == "textDocument/formatting") {
            if (idv) Formatear(*idv, P);
            return;
        }
        if (StartsWith(m, "$/")) return;
        if (m == "workspace/didChangeConfiguration" ||
            m == "workspace/didChangeWatchedFiles")
            return;
        if (idv) ResponderError(*idv, -32601, "método no soportado: " + m);
    }
};

bool LeerMensaje(std::string& out) {
    std::string linea;
    size_t longitud = 0;
    bool vioCabecera = false;
    while (true) {
        if (!std::getline(std::cin, linea)) return false;
        if (!linea.empty() && linea.back() == '\r') linea.pop_back();
        if (linea.empty()) break;
        vioCabecera = true;
        if (StartsWith(linea, "Content-Length:")) {
            std::string v = Trim(linea.substr(15));
            longitud = (size_t)strtoull(v.c_str(), nullptr, 10);
        }
    }
    if (!vioCabecera) return false;
    if (longitud == 0 || longitud > 64u * 1024 * 1024) return false;
    std::string cuerpo(longitud, '\0');
    std::cin.read(&cuerpo[0], (std::streamsize)longitud);
    if ((size_t)std::cin.gcount() != longitud) return false;
    out = std::move(cuerpo);
    return true;
}
}
int RunLsp() {
    std::ios::sync_with_stdio(false);
    ServidorLsp S;
    while (true) {
        std::string cuerpo;
        if (!LeerMensaje(cuerpo)) return 0;
        JsonValue msg;
        std::string err;
        if (!JsonParse(cuerpo, msg, err)) {
            JsonValue e = JsonValue::MakeObj();
            e.Set("code", JsonValue::MakeInt(-32700));
            e.Set("message", JsonValue::MakeText("JSON mal formado: " + err));
            JsonValue r = JsonValue::MakeObj();
            r.Set("jsonrpc", JsonValue::MakeText("2.0"));
            r.Set("id", JsonValue::MakeNull());
            r.Set("error", e);
            S.EnviarJson(r);
            continue;
        }
        S.Despachar(msg);
    }
}
}
