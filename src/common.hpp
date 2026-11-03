#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
namespace sylt {
struct Span {
    int Line = 0;
    int Col = 0;
    int Len = 0;
    Span() = default;
    Span(int line, int col, int len) : Line(line), Col(col), Len(len) {}
};
struct SourceFile {
    std::string Name;
    std::string Text;
    std::vector<std::string> Lines;
    void Load(const std::string& name, const std::string& text) {
        Name = name;
        Text = text;
        Lines.clear();
        std::string cur;
        for (char c : text) {
            if (c == '\n') {
                Lines.push_back(cur);
                cur.clear();
            } else {
                cur.push_back(c);
            }
        }
        if (!cur.empty()) Lines.push_back(cur);
    }
    std::string LineAt(int line) const {
        if (line >= 1 && line <= (int)Lines.size()) return Lines[line - 1];
        return "";
    }
};
inline bool StartsWith(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}
inline std::string Trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) b--;
    return s.substr(a, b - a);
}
inline int Levenshtein(const std::string& a, const std::string& b) {
    const size_t n = a.size(), m = b.size();
    std::vector<int> prev(m + 1), cur(m + 1);
    for (size_t j = 0; j <= m; j++) prev[j] = (int)j;
    for (size_t i = 1; i <= n; i++) {
        cur[0] = (int)i;
        for (size_t j = 1; j <= m; j++) {
            int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            int x = prev[j] + 1;
            int y = cur[j - 1] + 1;
            int z = prev[j - 1] + cost;
            cur[j] = x < y ? (x < z ? x : z) : (y < z ? y : z);
        }
        prev = cur;
    }
    return prev[m];
}
inline bool CloseEnough(const std::string& a, const std::string& b) {
    if (a == b) return false;
    int d = Levenshtein(a, b);
    int tol = (int)(a.size() / 3);
    if (tol < 2) tol = 2;
    return d <= tol;
}
}
