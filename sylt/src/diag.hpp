#pragma once
#include <cstdio>
#include <string>
#include <vector>
#include "common.hpp"
namespace sylt {
struct Diag {
    enum class Sev { Error, Warning };
    enum class NoteKind { Primary, Note };
    struct Note {
        NoteKind Kind;
        std::string Text;
        Span Sp;
    };
    Sev Severity = Sev::Error;
    std::string Code;
    std::string Msg;
    Span Primary;
    bool PrimaryValid = false;
    std::vector<Note> Notes;
    std::vector<std::string> Helps;
    const SourceFile* File = nullptr;
};
class Reporter {
public:
    explicit Reporter(const SourceFile& src, bool color)
        : Src(src), Cur(&src), Color(color) {}
    Diag Err(const std::string& code, Span sp, const std::string& msg) const {
        Diag d;
        d.Severity = Diag::Sev::Error;
        d.Code = code;
        d.Msg = msg;
        d.Primary = sp;
        d.PrimaryValid = true;
        d.File = Cur;
        return d;
    }
    Diag Warn(const std::string& code, Span sp, const std::string& msg) const {
        Diag d;
        d.Severity = Diag::Sev::Warning;
        d.Code = code;
        d.Msg = msg;
        d.Primary = sp;
        d.PrimaryValid = true;
        d.File = Cur;
        return d;
    }
    Diag ErrNoSpan(const std::string& code, const std::string& msg) const {
        Diag d = Err(code, Span(), msg);
        d.PrimaryValid = false;
        return d;
    }
    void Emit(const Diag& d);
    int ErrorCount() const { return Errors; }
    int WarningCount() const { return Warnings; }
    void Use(const SourceFile* f) { Cur = f ? f : &Src; }
    void SetSink(std::vector<Diag>* sink) { Sink = sink; }
    int Quiet = 0;
private:
    void RenderBox(const SourceFile& F, const Diag::Note& n, FILE* out) const;
    const SourceFile& Src;
    const SourceFile* Cur;
    bool Color;
    int Errors = 0;
    int Warnings = 0;
    std::vector<Diag>* Sink = nullptr;
};
struct Palette {
    std::string Err, Warn, Note, Help, Dim, Caret, Reset;
    static Palette Make(bool color);
};
}
