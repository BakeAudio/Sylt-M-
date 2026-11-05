#include "diag.hpp"
#include <cstdio>
namespace sylt {
static size_t Utf8SeqLen(unsigned char c) {
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}
static size_t CodepointsBefore(const std::string& s, size_t bytes) {
    size_t w = 0;
    size_t i = 0;
    while (i < s.size() && i < bytes) {
        i += Utf8SeqLen((unsigned char)s[i]);
        w++;
    }
    return w;
}
static size_t CodepointsInRange(const std::string& s, size_t from, size_t bytes) {
    size_t w = 0;
    size_t i = from;
    while (i < s.size() && i < from + bytes) {
        i += Utf8SeqLen((unsigned char)s[i]);
        w++;
    }
    return w;
}
Palette Palette::Make(bool color) {
    if (!color) return {"", "", "", "", "", "", ""};
    return {
        "\033[1;31m",
        "\033[1;33m",
        "\033[1;36m",
        "\033[1;32m",
        "\033[2m",
        "\033[1;31m",
        "\033[0m",
    };
}
void Reporter::Emit(const Diag& d) {
    if (Quiet > 0) return;
    if (Sink) {
        if (d.Severity == Diag::Sev::Error) Errors++;
        else Warnings++;
        Sink->push_back(d);
        return;
    }
    Palette P = Palette::Make(Color);
    const SourceFile& F = d.File ? *d.File : Src;
    FILE* out = (d.Severity == Diag::Sev::Warning) ? stderr : stdout;
    const char* sev = (d.Severity == Diag::Sev::Error) ? "error" : "aviso";
    std::string col = (d.Severity == Diag::Sev::Error) ? P.Err : P.Warn;
    if (d.PrimaryValid) {
        std::fprintf(out, "%s%s[%s]%s: %s\n", col.c_str(), sev, d.Code.c_str(),
                     P.Reset.c_str(), d.Msg.c_str());
        Diag::Note primary{Diag::NoteKind::Primary, "", d.Primary};
        RenderBox(F, primary, out);
        for (const auto& n : d.Notes) {
            std::fprintf(out, "%snota:%s %s\n", P.Note.c_str(), P.Reset.c_str(), n.Text.c_str());
            RenderBox(F, n, out);
        }
    } else {
        std::fprintf(out, "%s%s[%s]%s: %s\n", col.c_str(), sev, d.Code.c_str(),
                     P.Reset.c_str(), d.Msg.c_str());
    }
    for (const auto& h : d.Helps) {
        std::fprintf(out, "%sayuda:%s %s\n", P.Help.c_str(), P.Reset.c_str(), h.c_str());
    }
    std::fprintf(out, "\n");
    if (d.Severity == Diag::Sev::Error) Errors++; else Warnings++;
}
void Reporter::RenderBox(const SourceFile& F, const Diag::Note& n, FILE* out) const {
    Palette P = Palette::Make(Color);
    if (!n.Sp.Line || n.Sp.Line > (int)F.Lines.size()) return;
    int ln = n.Sp.Line;
    std::string line = F.LineAt(ln);
    char numbuf[16];
    std::snprintf(numbuf, sizeof(numbuf), "%d", ln);
    std::string num(numbuf);
    std::string gutter = std::string(num.size(), ' ');
    int col = n.Sp.Col >= 1 ? n.Sp.Col : 1;
    int len = n.Sp.Len > 0 ? n.Sp.Len : 1;
    if (len < 1) len = 1;
    int dcol = 1 + (int)CodepointsBefore(line, (size_t)(col - 1));
    int dlen = (int)CodepointsInRange(line, (size_t)(col - 1), (size_t)len);
    if (dlen < 1) dlen = 1;
    std::fprintf(out, "%s  ┌─ %s:%d:%d%s\n", P.Dim.c_str(), F.Name.c_str(), ln, dcol,
                 P.Reset.c_str());
    std::fprintf(out, "%s  │%s\n", P.Dim.c_str(), P.Reset.c_str());
    std::fprintf(out, " %s %s│ %s%s\n", num.c_str(), P.Dim.c_str(), P.Reset.c_str(),
                 line.c_str());
    std::string pad((size_t)(dcol - 1), ' ');
    std::string carets((size_t)dlen, '^');
    std::string label = n.Text.empty() ? "" : " " + n.Text;
    std::fprintf(out, " %s %s│ %s%s%s%s%s%s\n", gutter.c_str(), P.Dim.c_str(), P.Reset.c_str(),
                 pad.c_str(), P.Caret.c_str(), carets.c_str(), P.Reset.c_str(),
                 label.c_str());
    std::fprintf(out, "%s  │%s\n", P.Dim.c_str(), P.Reset.c_str());
}
}
