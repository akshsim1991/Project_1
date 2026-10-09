// PdfCrypt.cpp - AES-256 PDF encryption (ISO 32000-2, 7.6.4.3 and 7.6.4.4).
#include "PdfCrypt.h"

#include "Common.h"

#include <bcrypt.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <vector>

namespace {
using Bytes = std::vector<uint8_t>;

// --- Windows CNG (bcrypt.dll) ------------------------------------------------
class Algorithm {
public:
    Algorithm(const wchar_t* id, const wchar_t* chain = nullptr) {
        if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&m_alg, id, nullptr, 0))) {
            m_alg = nullptr;
            return;
        }
        if (chain && !BCRYPT_SUCCESS(BCryptSetProperty(m_alg, BCRYPT_CHAINING_MODE, (PUCHAR)chain,
                                                       (ULONG)((wcslen(chain) + 1) * sizeof(wchar_t)), 0))) {
            BCryptCloseAlgorithmProvider(m_alg, 0);
            m_alg = nullptr;
        }
    }
    ~Algorithm() {
        if (m_alg) BCryptCloseAlgorithmProvider(m_alg, 0);
    }
    Algorithm(const Algorithm&) = delete;
    Algorithm& operator=(const Algorithm&) = delete;
    BCRYPT_ALG_HANDLE Get() const { return m_alg; }

private:
    BCRYPT_ALG_HANDLE m_alg = nullptr;
};

struct Crypto {
    Algorithm sha256{BCRYPT_SHA256_ALGORITHM}, sha384{BCRYPT_SHA384_ALGORITHM}, sha512{BCRYPT_SHA512_ALGORITHM};
    Algorithm cbc{BCRYPT_AES_ALGORITHM, BCRYPT_CHAIN_MODE_CBC}, ecb{BCRYPT_AES_ALGORITHM, BCRYPT_CHAIN_MODE_ECB};
    bool Ready() const { return sha256.Get() && sha384.Get() && sha512.Get() && cbc.Get() && ecb.Get(); }

    bool Hash(const Algorithm& alg, const uint8_t* data, size_t n, Bytes& out) const {
        BCRYPT_HASH_HANDLE h = nullptr;
        DWORD len = 0, got = 0;
        if (!BCRYPT_SUCCESS(BCryptGetProperty(alg.Get(), BCRYPT_HASH_LENGTH, (PUCHAR)&len, sizeof(len), &got, 0)) ||
            !BCRYPT_SUCCESS(BCryptCreateHash(alg.Get(), &h, nullptr, 0, nullptr, 0, 0)))
            return false;
        out.assign(len, 0);
        const bool ok = BCRYPT_SUCCESS(BCryptHashData(h, (PUCHAR)data, (ULONG)n, 0)) &&
                        BCRYPT_SUCCESS(BCryptFinishHash(h, out.data(), len, 0));
        BCryptDestroyHash(h);
        return ok;
    }

    // AES with a `keyLen`-byte key; CBC with `iv` (16 bytes) or ECB when
    // iv is null. With `pad`, PKCS#7 padding is added.
    bool Aes(const uint8_t* key, size_t keyLen, const uint8_t* iv, const uint8_t* in, size_t n, bool pad,
             Bytes& out) const {
        BCRYPT_KEY_HANDLE k = nullptr;
        if (!BCRYPT_SUCCESS(BCryptGenerateSymmetricKey((iv ? cbc : ecb).Get(), &k, nullptr, 0, (PUCHAR)key,
                                                       (ULONG)keyLen, 0)))
            return false;
        uint8_t ivCopy[16];
        if (iv) memcpy(ivCopy, iv, 16);
        const ULONG flags = pad ? BCRYPT_BLOCK_PADDING : 0;
        ULONG size = 0;
        bool ok = BCRYPT_SUCCESS(BCryptEncrypt(k, (PUCHAR)in, (ULONG)n, nullptr, iv ? ivCopy : nullptr,
                                               iv ? 16 : 0, nullptr, 0, &size, flags));
        if (ok) {
            out.assign(size, 0);
            if (iv) memcpy(ivCopy, iv, 16);
            ok = BCRYPT_SUCCESS(BCryptEncrypt(k, (PUCHAR)in, (ULONG)n, nullptr, iv ? ivCopy : nullptr, iv ? 16 : 0,
                                              out.data(), size, &size, flags));
            out.resize(size);
        }
        BCryptDestroyKey(k);
        return ok;
    }
};

bool Random(uint8_t* buf, size_t n) {
    return BCRYPT_SUCCESS(BCryptGenRandom(nullptr, buf, (ULONG)n, BCRYPT_USE_SYSTEM_PREFERRED_RNG));
}

// Algorithm 2.B: the hash of a password with a salt (and the U string for
// the owner password).
bool PasswordHash(const Crypto& c, const std::string& password, const uint8_t* salt, const Bytes& udata,
                  Bytes& out) {
    Bytes in(password.begin(), password.end());
    in.insert(in.end(), salt, salt + 8);
    in.insert(in.end(), udata.begin(), udata.end());
    Bytes k;
    if (!c.Hash(c.sha256, in.data(), in.size(), k)) return false;
    Bytes e;
    for (int i = 0; i < 64 || e.back() > i - 32; ++i) {
        Bytes block(password.begin(), password.end());
        block.insert(block.end(), k.begin(), k.end());
        block.insert(block.end(), udata.begin(), udata.end());
        Bytes k1;
        k1.reserve(block.size() * 64);
        for (int r = 0; r < 64; ++r) k1.insert(k1.end(), block.begin(), block.end());
        if (!c.Aes(k.data(), 16, k.data() + 16, k1.data(), k1.size(), false, e)) return false;
        unsigned sum = 0;
        for (int b = 0; b < 16; ++b) sum += e[(size_t)b];
        const Algorithm& next = sum % 3 == 0 ? c.sha256 : sum % 3 == 1 ? c.sha384 : c.sha512;
        if (!c.Hash(next, e.data(), e.size(), k)) return false;
    }
    out.assign(k.begin(), k.begin() + 32);
    return true;
}

std::string Hex(const uint8_t* p, size_t n) {
    static const char kDigits[] = "0123456789ABCDEF";
    std::string s;
    s.reserve(n * 2 + 2);
    s += '<';
    for (size_t i = 0; i < n; ++i) {
        s += kDigits[p[i] >> 4];
        s += kDigits[p[i] & 15];
    }
    s += '>';
    return s;
}

// --- reading PDFium's output -------------------------------------------------
bool IsWhite(char c) { return c == ' ' || c == '\r' || c == '\n' || c == '\t' || c == '\f' || c == '\0'; }
bool IsDelim(char c) { return strchr("()<>[]{}/%", c) != nullptr && c != 0; }

struct Token {
    enum Kind { Other, Name, Number, Keyword, String, HexString, DictOpen, DictClose, ArrayOpen, ArrayClose } kind = Other;
    size_t start = 0, end = 0;  // bytes [start, end) of the source
};

class Lexer {
public:
    Lexer(const std::string& s, size_t pos, size_t limit) : m_s(s), m_pos(pos), m_limit(limit) {}
    size_t Pos() const { return m_pos; }
    void Seek(size_t pos) { m_pos = pos; }

    bool Next(Token& t) {
        // Whitespace and comments.
        for (;;) {
            while (m_pos < m_limit && IsWhite(m_s[m_pos])) ++m_pos;
            if (m_pos < m_limit && m_s[m_pos] == '%') {
                while (m_pos < m_limit && m_s[m_pos] != '\r' && m_s[m_pos] != '\n') ++m_pos;
                continue;
            }
            break;
        }
        if (m_pos >= m_limit) return false;
        t.start = m_pos;
        const char c = m_s[m_pos];
        if (c == '(') {
            int depth = 0;
            for (; m_pos < m_limit; ++m_pos) {
                const char d = m_s[m_pos];
                if (d == '\\') {
                    ++m_pos;
                } else if (d == '(') {
                    ++depth;
                } else if (d == ')' && --depth == 0) {
                    ++m_pos;
                    break;
                }
            }
            t.kind = Token::String;
        } else if (c == '<' && m_pos + 1 < m_limit && m_s[m_pos + 1] == '<') {
            m_pos += 2;
            t.kind = Token::DictOpen;
        } else if (c == '>' && m_pos + 1 < m_limit && m_s[m_pos + 1] == '>') {
            m_pos += 2;
            t.kind = Token::DictClose;
        } else if (c == '<') {
            while (m_pos < m_limit && m_s[m_pos] != '>') ++m_pos;
            ++m_pos;
            t.kind = Token::HexString;
        } else if (c == '[' || c == ']' || c == '{' || c == '}') {
            ++m_pos;
            t.kind = c == '[' ? Token::ArrayOpen : c == ']' ? Token::ArrayClose : Token::Other;
        } else if (c == '/') {
            ++m_pos;
            while (m_pos < m_limit && !IsWhite(m_s[m_pos]) && !IsDelim(m_s[m_pos])) ++m_pos;
            t.kind = Token::Name;
        } else {
            while (m_pos < m_limit && !IsWhite(m_s[m_pos]) && !IsDelim(m_s[m_pos])) ++m_pos;
            if (m_pos == t.start) ++m_pos;  // a stray delimiter
            const char f = m_s[t.start];
            t.kind = (f >= '0' && f <= '9') || f == '-' || f == '+' || f == '.' ? Token::Number : Token::Keyword;
        }
        t.end = std::min(m_pos, m_limit);
        return true;
    }

    std::string Text(const Token& t) const { return m_s.substr(t.start, t.end - t.start); }

private:
    const std::string& m_s;
    size_t m_pos, m_limit;
};

// The bytes a literal "(...)" or hex "<...>" string stands for.
std::string DecodeString(const std::string& s, const Token& t) {
    std::string out;
    if (t.kind == Token::HexString) {
        int hi = -1;
        for (size_t i = t.start + 1; i + 1 < t.end; ++i) {
            const char c = s[i];
            int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (v < 0) continue;
            if (hi < 0) {
                hi = v;
            } else {
                out += (char)(hi * 16 + v);
                hi = -1;
            }
        }
        if (hi >= 0) out += (char)(hi * 16);
        return out;
    }
    for (size_t i = t.start + 1; i + 1 < t.end; ++i) {
        char c = s[i];
        if (c == '\r') {  // an end of line in a string is a single "\n"
            if (i + 2 < t.end && s[i + 1] == '\n') ++i;
            out += '\n';
            continue;
        }
        if (c != '\\') {
            out += c;
            continue;
        }
        if (++i + 1 > t.end - 1) break;
        c = s[i];
        switch (c) {
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case '\r':
                if (i + 2 < t.end && s[i + 1] == '\n') ++i;
                break;
            case '\n': break;
            default:
                if (c >= '0' && c <= '7') {
                    int v = 0;
                    for (int k = 0; k < 3 && i < t.end - 1 && s[i] >= '0' && s[i] <= '7'; ++k, ++i) v = v * 8 + (s[i] - '0');
                    --i;
                    out += (char)(v & 0xFF);
                } else {
                    out += c;  // \( \) \\ and unknown escapes
                }
        }
    }
    return out;
}

struct Xref {
    std::map<uint32_t, size_t> offsets;  // object number -> offset (in-use objects)
    std::string root, info, id;          // "n g R" / "[<..><..>]" from the trailer
};

bool ReadXref(const std::string& s, Xref& x) {
    const size_t sx = s.rfind("startxref");
    if (sx == std::string::npos) return false;
    const size_t at = (size_t)strtoull(s.c_str() + sx + 9, nullptr, 10);
    if (at >= s.size() || s.compare(at, 4, "xref") != 0) return false;  // an xref stream: not PDFium's
    size_t pos = at + 4;
    for (;;) {
        while (pos < s.size() && IsWhite(s[pos])) ++pos;
        if (pos >= s.size()) return false;
        if (s.compare(pos, 7, "trailer") == 0) {
            pos += 7;
            break;
        }
        const char* start = s.c_str() + pos;
        char* end = nullptr;
        const unsigned long first = strtoul(start, &end, 10);
        if (end == start) return false;
        const unsigned long count = strtoul(end, &end, 10);
        pos = (size_t)(end - s.c_str());
        for (unsigned long i = 0; i < count; ++i) {
            while (pos < s.size() && IsWhite(s[pos])) ++pos;
            if (pos + 18 > s.size()) return false;
            const size_t offset = (size_t)strtoull(s.c_str() + pos, nullptr, 10);
            const char type = s[pos + 17];
            if (type == 'n' && first + i != 0) x.offsets[(uint32_t)(first + i)] = offset;
            pos += 18;
        }
    }
    // The trailer dictionary: keep /Root, /Info and /ID.
    Lexer lex(s, pos, s.size());
    Token t;
    int depth = 0;
    std::string key;
    while (lex.Next(t)) {
        if (t.kind == Token::DictOpen) {
            ++depth;
            continue;
        }
        if (t.kind == Token::DictClose && --depth <= 0) break;
        if (depth != 1) continue;
        if (t.kind == Token::Name && key.empty()) {
            key = lex.Text(t);
            continue;
        }
        if (key == "/Root" || key == "/Info") {
            Token g, r;
            if (!lex.Next(g) || !lex.Next(r)) return false;
            (key == "/Root" ? x.root : x.info) = s.substr(t.start, r.end - t.start);
        } else if (key == "/ID" && t.kind == Token::ArrayOpen) {
            const size_t start = t.start;
            while (lex.Next(t) && t.kind != Token::ArrayClose) {}
            x.id = s.substr(start, t.end - start);
        } else if (t.kind == Token::DictOpen || t.kind == Token::ArrayOpen) {
            // a nested value we do not need: skip it
            int nest = 1;
            while (nest > 0 && lex.Next(t)) {
                if (t.kind == Token::DictOpen || t.kind == Token::ArrayOpen) ++nest;
                if (t.kind == Token::DictClose || t.kind == Token::ArrayClose) --nest;
            }
        } else if (t.kind == Token::Number) {
            // "n g R" values (/Encrypt of the old file would be one)
            const size_t save = lex.Pos();
            Token g, r;
            if (!(lex.Next(g) && g.kind == Token::Number && lex.Next(r) && lex.Text(r) == "R")) lex.Seek(save);
        }
        key.clear();
    }
    return !x.root.empty() && !x.offsets.empty();
}

// The integer an object holds ("12 0 obj 345 endobj" -> 345), for indirect /Length values.
bool ObjectNumber(const std::string& s, const Xref& x, uint32_t num, long long& value) {
    auto it = x.offsets.find(num);
    if (it == x.offsets.end()) return false;
    Lexer lex(s, it->second, s.size());
    Token a, b, c, v;
    if (!lex.Next(a) || !lex.Next(b) || !lex.Next(c) || lex.Text(c) != "obj" || !lex.Next(v) || v.kind != Token::Number)
        return false;
    value = strtoll(lex.Text(v).c_str(), nullptr, 10);
    return true;
}
}  // namespace

bool EncryptPdf(const std::string& in, const std::string& userPassword, const std::string& ownerPassword,
                uint32_t permissions, std::string& out) {
    Crypto c;
    if (!c.Ready()) return false;
    Xref x;
    if (!ReadXref(in, x)) return false;

    // Keys and the /Encrypt dictionary values (Algorithms 8, 9 and 10).
    const std::string user = userPassword.substr(0, 127);
    std::string owner = ownerPassword.substr(0, 127);
    if (owner.empty()) {  // nobody needs to lift restrictions that are not there
        uint8_t r[24];
        if (!Random(r, sizeof(r))) return false;
        owner = Hex(r, sizeof(r));
    }
    uint8_t fileKey[32], salts[32];
    if (!Random(fileKey, 32) || !Random(salts, 32)) return false;
    Bytes h, empty, u, ue, o, oe, perms;
    if (!PasswordHash(c, user, salts, empty, h)) return false;
    u = h;
    u.insert(u.end(), salts, salts + 16);  // validation salt, key salt
    const uint8_t zeroIv[16] = {};
    if (!PasswordHash(c, user, salts + 8, empty, h) || !c.Aes(h.data(), 32, zeroIv, fileKey, 32, false, ue))
        return false;
    if (!PasswordHash(c, owner, salts + 16, u, h)) return false;
    o = h;
    o.insert(o.end(), salts + 16, salts + 32);
    if (!PasswordHash(c, owner, salts + 24, u, h) || !c.Aes(h.data(), 32, zeroIv, fileKey, 32, false, oe))
        return false;
    // Bits 1-2 must be 0, 7-8 and 13-32 must be 1.
    const uint32_t p = (permissions | 0xFFFFF0C0u) & ~3u;
    uint8_t permsPlain[16] = {(uint8_t)p, (uint8_t)(p >> 8), (uint8_t)(p >> 16), (uint8_t)(p >> 24),
                              0xFF, 0xFF, 0xFF, 0xFF, 'T', 'a', 'd', 'b'};
    if (!Random(permsPlain + 12, 4) || !c.Aes(fileKey, 32, nullptr, permsPlain, 16, false, perms)) return false;

    auto encrypt = [&](const std::string& plain, std::string& result) {
        uint8_t iv[16];
        Bytes enc;
        if (!Random(iv, 16) || !c.Aes(fileKey, 32, iv, (const uint8_t*)plain.data(), plain.size(), true, enc))
            return false;
        result.assign((const char*)iv, 16);
        result.append((const char*)enc.data(), enc.size());
        return true;
    };

    // The header, with a version that knows AES-256.
    const size_t firstObj = x.offsets.begin()->second;
    std::string header = in.substr(0, std::min(firstObj, in.size()));
    if (header.compare(0, 5, "%PDF-") != 0) return false;
    if (header.size() >= 8 && header[5] == '1' && header[7] < '7') header[7] = '7';
    out.clear();
    out.reserve(in.size() + in.size() / 20 + 4096);
    out += header;

    std::map<uint32_t, size_t> written;
    std::map<uint32_t, std::string> generations;
    uint32_t maxNum = 0;
    for (const auto& entry : x.offsets) {
        const uint32_t num = entry.first;
        maxNum = std::max(maxNum, num);
        Lexer lex(in, entry.second, in.size());
        Token tn, tg, tobj;
        if (!lex.Next(tn) || !lex.Next(tg) || !lex.Next(tobj) || lex.Text(tobj) != "obj") return false;
        const size_t bodyStart = lex.Pos();

        // Find the end of the object's value (and a stream's data).
        std::vector<Token> tokens;
        Token t;
        size_t bodyEnd = std::string::npos, streamStart = 0, streamLen = 0;
        bool isStream = false;
        int lengthValue = -1;  // index in `tokens` of the /Length value
        int depth = 0;
        bool xrefStream = false;
        while (lex.Next(t)) {
            const std::string text = t.kind == Token::Keyword ? lex.Text(t) : std::string();
            if (text == "endobj") {
                bodyEnd = t.start;
                break;
            }
            if (text == "stream") {
                isStream = true;
                bodyEnd = t.start;
                size_t pos = t.end;
                if (pos < in.size() && in[pos] == '\r') ++pos;
                if (pos < in.size() && in[pos] == '\n') ++pos;
                streamStart = pos;
                break;
            }
            if (t.kind == Token::DictOpen || t.kind == Token::ArrayOpen) ++depth;
            if (t.kind == Token::DictClose || t.kind == Token::ArrayClose) --depth;
            tokens.push_back(t);
            if (depth == 1 && tokens.size() >= 2 && tokens[tokens.size() - 2].kind == Token::Name &&
                lex.Text(tokens[tokens.size() - 2]) == "/Length" && t.kind == Token::Number && lengthValue < 0)
                lengthValue = (int)tokens.size() - 1;
            if (depth == 1 && tokens.size() >= 2 && lex.Text(tokens[tokens.size() - 2]) == "/Type" &&
                lex.Text(t) == "/XRef")
                xrefStream = true;
        }
        if (bodyEnd == std::string::npos) return false;

        std::string data;
        size_t lengthEnd = 0;  // where the /Length value ends ("n g R" or "n")
        if (isStream) {
            if (lengthValue < 0) return false;
            long long len = strtoll(lex.Text(tokens[(size_t)lengthValue]).c_str(), nullptr, 10);
            lengthEnd = tokens[(size_t)lengthValue].end;
            if ((size_t)lengthValue + 2 < tokens.size() && lex.Text(tokens[(size_t)lengthValue + 2]) == "R") {
                lengthEnd = tokens[(size_t)lengthValue + 2].end;
                if (!ObjectNumber(in, x, (uint32_t)len, len)) return false;
            }
            if (len < 0 || streamStart + (size_t)len > in.size()) return false;
            size_t after = streamStart + (size_t)len;
            while (after < in.size() && IsWhite(in[after])) ++after;
            if (in.compare(after, 9, "endstream") != 0) {
                // A wrong /Length: the data runs up to "endstream".
                const size_t es = in.find("endstream", streamStart);
                if (es == std::string::npos) return false;
                size_t e = es;
                if (e > streamStart && in[e - 1] == '\n') --e;
                if (e > streamStart && in[e - 1] == '\r') --e;
                len = (long long)(e - streamStart);
            }
            streamLen = (size_t)len;
            if (xrefStream) continue;  // cross-reference streams are never kept
            if (!encrypt(in.substr(streamStart, streamLen), data)) return false;
        }

        // Write the object: strings encrypted (as hex), the rest as it was.
        written[num] = out.size();
        generations[num] = lex.Text(tg);
        out += lex.Text(tn) + " " + lex.Text(tg) + " obj\r\n";
        size_t copied = bodyStart;
        for (size_t i = 0; i < tokens.size(); ++i) {
            const Token& k = tokens[i];
            if (k.kind == Token::String || k.kind == Token::HexString) {
                std::string enc;
                if (!encrypt(DecodeString(in, k), enc)) return false;
                out.append(in, copied, k.start - copied);
                out += Hex((const uint8_t*)enc.data(), enc.size());
                copied = k.end;
            } else if (isStream && (int)i == lengthValue) {
                out.append(in, copied, k.start - copied);
                out += std::to_string(data.size());
                copied = lengthEnd;
                // skip the "g R" of an indirect length
                while (i + 1 < tokens.size() && tokens[i + 1].end <= lengthEnd) ++i;
            }
        }
        out.append(in, copied, bodyEnd - copied);
        if (isStream) {
            out += "stream\r\n";
            out += data;
            out += "\r\nendstream";
        }
        out += "\r\nendobj\r\n";
    }

    // The /Encrypt dictionary.
    const uint32_t encNum = maxNum + 1;
    written[encNum] = out.size();
    const int32_t pSigned = (int32_t)p;
    out += std::to_string(encNum) +
           " 0 obj\r\n<</Filter/Standard/V 5/R 6/Length 256/CF<</StdCF<</AuthEvent/DocOpen/CFM/AESV3/Length 32>>>>"
           "/StmF/StdCF/StrF/StdCF/EncryptMetadata true/P " +
           std::to_string(pSigned) + "/O" + Hex(o.data(), o.size()) + "/U" + Hex(u.data(), u.size()) + "/OE" +
           Hex(oe.data(), oe.size()) + "/UE" + Hex(ue.data(), ue.size()) + "/Perms" +
           Hex(perms.data(), perms.size()) + ">>\r\nendobj\r\n";

    // Cross-reference table: one section per run of consecutive numbers.
    const size_t xrefAt = out.size();
    out += "xref\r\n0 1\r\n0000000000 65535 f\r\n";
    for (auto it = written.begin(); it != written.end();) {
        auto run = it;
        uint32_t next = it->first;
        size_t n = 0;
        while (run != written.end() && run->first == next) {
            ++run;
            ++next;
            ++n;
        }
        out += std::to_string(it->first) + " " + std::to_string(n) + "\r\n";
        for (; it != run; ++it) {
            char line[32];
            const auto gen = generations.find(it->first);
            snprintf(line, sizeof(line), "%010llu %05d n\r\n", (unsigned long long)it->second,
                     gen == generations.end() ? 0 : atoi(gen->second.c_str()));
            out += line;
        }
    }
    std::string id = x.id;
    if (id.empty()) {
        uint8_t r[16];
        if (!Random(r, 16)) return false;
        id = "[" + Hex(r, 16) + Hex(r, 16) + "]";
    }
    out += "trailer\r\n<</Size " + std::to_string(encNum + 1) + "/Root " + x.root +
           (x.info.empty() ? "" : "/Info " + x.info) + "/Encrypt " + std::to_string(encNum) + " 0 R/ID" + id +
           ">>\r\nstartxref\r\n" + std::to_string(xrefAt) + "\r\n%%EOF\r\n";
    return true;
}
