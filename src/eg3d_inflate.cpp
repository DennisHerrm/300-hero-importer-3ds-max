#include "eg3d_inflate.h"

#include <cstring>

namespace eg3d {
namespace {

// ------------------------------------------------------------
//  Bitleser, LSB zuerst - so schreibt DEFLATE.
// ------------------------------------------------------------
class BitReader {
public:
    BitReader(const uint8_t* data, size_t size)
        : data_(data), size_(size), pos_(0), bitBuffer_(0), bitCount_(0) {}

    // Wirft nicht: der Aufrufer prueft Failed() an den
    // Blockgrenzen. Eine Ausnahme je Bit waere hier zu teuer.
    uint32_t Bits(int count) {
        while (bitCount_ < count) {
            if (pos_ >= size_) { failed_ = true; return 0; }
            bitBuffer_ |= static_cast<uint32_t>(data_[pos_++]) << bitCount_;
            bitCount_ += 8;
        }
        const uint32_t value = bitBuffer_ & ((1u << count) - 1u);
        bitBuffer_ >>= count;
        bitCount_ -= count;
        return value;
    }

    void AlignToByte() { bitBuffer_ = 0; bitCount_ = 0; }

    // Byteposition nach dem Ausrichten. Nur nach AlignToByte()
    // sinnvoll - vorher stecken noch Bits im Puffer.
    size_t BytePos() const { return pos_; }

    bool   Failed() const { return failed_; }
    size_t Size() const   { return size_; }

    const uint8_t* At(size_t off) const { return data_ + off; }
    void SetBytePos(size_t p) { pos_ = p; }

private:
    const uint8_t* data_;
    size_t         size_;
    size_t         pos_;
    uint32_t       bitBuffer_;
    int            bitCount_;
    bool           failed_ = false;
};

// ------------------------------------------------------------
//  Kanonischer Huffman-Dekoder
//
//  Aufbau nach RFC 1951 Abschnitt 3.2.2: aus den Codelaengen die
//  Startcodes je Laenge bilden, dann Bit fuer Bit vergleichen.
//  Bewusst die einfache Variante ohne Nachschlagetabelle - die
//  Bloecke hier sind wenige Megabyte, und ein Fehler in einer
//  handgebauten Tabelle waere schwer zu finden.
// ------------------------------------------------------------
class Huffman {
public:
    static const int kMaxBits = 15;

    bool Build(const uint8_t* lengths, int count) {
        std::memset(countPerLength_, 0, sizeof countPerLength_);
        symbols_.assign(static_cast<size_t>(count), 0);

        for (int i = 0; i < count; ++i) {
            if (lengths[i] > kMaxBits) return false;
            ++countPerLength_[lengths[i]];
        }
        // Laenge 0 heisst "kommt nicht vor".
        countPerLength_[0] = 0;

        // Ueberzaehlig? Dann waere der Code nicht praefixfrei.
        int left = 1;
        for (int len = 1; len <= kMaxBits; ++len) {
            left <<= 1;
            left -= countPerLength_[len];
            if (left < 0) return false;
        }

        int offsets[kMaxBits + 2];
        offsets[1] = 0;
        for (int len = 1; len <= kMaxBits; ++len)
            offsets[len + 1] = offsets[len] + countPerLength_[len];

        for (int i = 0; i < count; ++i)
            if (lengths[i] != 0)
                symbols_[static_cast<size_t>(offsets[lengths[i]]++)] =
                    static_cast<uint16_t>(i);
        return true;
    }

    // -1 = ungueltiger Code
    int Decode(BitReader& in) const {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len <= kMaxBits; ++len) {
            code |= static_cast<int>(in.Bits(1));
            const int count = countPerLength_[len];
            if (code - first < count)
                return symbols_[static_cast<size_t>(index + (code - first))];
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
        }
        return -1;
    }

private:
    int                   countPerLength_[kMaxBits + 1] = {0};
    std::vector<uint16_t> symbols_;
};

const uint16_t kLengthBase[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};
const uint8_t kLengthExtra[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
};
const uint16_t kDistBase[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
    8193, 12289, 16385, 24577
};
const uint8_t kDistExtra[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};
const uint8_t kCodeLengthOrder[19] = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
};

void BuildFixedTables(Huffman& lit, Huffman& dist) {
    uint8_t litLengths[288];
    for (int i = 0;   i < 144; ++i) litLengths[i] = 8;
    for (int i = 144; i < 256; ++i) litLengths[i] = 9;
    for (int i = 256; i < 280; ++i) litLengths[i] = 7;
    for (int i = 280; i < 288; ++i) litLengths[i] = 8;
    lit.Build(litLengths, 288);

    uint8_t distLengths[30];
    for (int i = 0; i < 30; ++i) distLengths[i] = 5;
    dist.Build(distLengths, 30);
}

bool ReadDynamicTables(BitReader& in, Huffman& lit, Huffman& dist,
                       std::string& error) {
    const int hlit  = static_cast<int>(in.Bits(5)) + 257;
    const int hdist = static_cast<int>(in.Bits(5)) + 1;
    const int hclen = static_cast<int>(in.Bits(4)) + 4;
    if (in.Failed()) { error = "Stream endet im Tabellenkopf"; return false; }
    if (hlit > 286 || hdist > 30) { error = "Tabellengroessen ausserhalb des Standards"; return false; }

    uint8_t codeLengths[19];
    std::memset(codeLengths, 0, sizeof codeLengths);
    for (int i = 0; i < hclen; ++i)
        codeLengths[kCodeLengthOrder[i]] = static_cast<uint8_t>(in.Bits(3));

    Huffman lengthTable;
    if (!lengthTable.Build(codeLengths, 19)) {
        error = "Codelaengen-Tabelle ist nicht praefixfrei";
        return false;
    }

    std::vector<uint8_t> lengths(static_cast<size_t>(hlit + hdist), 0);
    int written = 0;
    while (written < hlit + hdist) {
        const int symbol = lengthTable.Decode(in);
        if (symbol < 0 || in.Failed()) { error = "ungueltiger Codelaengen-Code"; return false; }

        if (symbol < 16) {
            lengths[static_cast<size_t>(written++)] = static_cast<uint8_t>(symbol);
        } else if (symbol == 16) {
            if (written == 0) { error = "Wiederholung ohne Vorgaenger"; return false; }
            const uint8_t previous = lengths[static_cast<size_t>(written - 1)];
            int repeat = 3 + static_cast<int>(in.Bits(2));
            if (written + repeat > hlit + hdist) { error = "Wiederholung laeuft ueber die Tabelle hinaus"; return false; }
            while (repeat-- > 0) lengths[static_cast<size_t>(written++)] = previous;
        } else {
            int repeat = (symbol == 17) ? (3 + static_cast<int>(in.Bits(3)))
                                        : (11 + static_cast<int>(in.Bits(7)));
            if (written + repeat > hlit + hdist) { error = "Nullfolge laeuft ueber die Tabelle hinaus"; return false; }
            while (repeat-- > 0) lengths[static_cast<size_t>(written++)] = 0;
        }
    }

    if (!lit.Build(lengths.data(), hlit)) { error = "Literal-Tabelle ist nicht praefixfrei"; return false; }
    if (!dist.Build(lengths.data() + hlit, hdist)) { error = "Distanz-Tabelle ist nicht praefixfrei"; return false; }
    return true;
}

} // namespace

uint32_t Adler32(const uint8_t* data, size_t size) {
    uint32_t a = 1, b = 0;
    // 5552 ist die groesste Blocklaenge, bei der b garantiert
    // nicht ueberlaeuft - danach wird modulo gerechnet.
    const size_t kChunk = 5552;
    size_t pos = 0;
    while (pos < size) {
        const size_t take = (size - pos < kChunk) ? (size - pos) : kChunk;
        for (size_t i = 0; i < take; ++i) { a += data[pos + i]; b += a; }
        a %= 65521u;
        b %= 65521u;
        pos += take;
    }
    return (b << 16) | a;
}

InflateResult RawInflate(const uint8_t* data, size_t size,
                         size_t expectedSize, std::vector<uint8_t>& out) {
    InflateResult result;
    out.clear();
    if (expectedSize > 0 && expectedSize < (size_t(1) << 31))
        out.reserve(expectedSize);

    BitReader in(data, size);
    Huffman fixedLit, fixedDist;
    BuildFixedTables(fixedLit, fixedDist);

    for (;;) {
        const int final = static_cast<int>(in.Bits(1));
        const int type  = static_cast<int>(in.Bits(2));
        if (in.Failed()) { result.error = "Stream endet vor dem Blockkopf"; return result; }

        if (type == 0) {
            // Unkomprimiert: auf Bytegrenze, dann LEN und ~LEN.
            in.AlignToByte();
            const size_t at = in.BytePos();
            if (at + 4 > size) { result.error = "unkomprimierter Block ohne Laengenfeld"; return result; }
            const uint16_t length = static_cast<uint16_t>(data[at] | (data[at + 1] << 8));
            const uint16_t inverse = static_cast<uint16_t>(data[at + 2] | (data[at + 3] << 8));
            if (static_cast<uint16_t>(~length) != inverse) {
                result.error = "unkomprimierter Block: LEN und ~LEN passen nicht zusammen";
                return result;
            }
            if (at + 4 + length > size) { result.error = "unkomprimierter Block laeuft ueber das Ende hinaus"; return result; }
            out.insert(out.end(), data + at + 4, data + at + 4 + length);
            in.SetBytePos(at + 4 + length);
        } else if (type == 1 || type == 2) {
            Huffman dynLit, dynDist;
            const Huffman* lit  = &fixedLit;
            const Huffman* dist = &fixedDist;
            if (type == 2) {
                if (!ReadDynamicTables(in, dynLit, dynDist, result.error)) return result;
                lit = &dynLit;
                dist = &dynDist;
            }

            for (;;) {
                const int symbol = lit->Decode(in);
                if (symbol < 0 || in.Failed()) { result.error = "ungueltiger Literal-Code"; return result; }
                if (symbol < 256) {
                    out.push_back(static_cast<uint8_t>(symbol));
                    continue;
                }
                if (symbol == 256) break;   // Blockende

                const int lengthIndex = symbol - 257;
                if (lengthIndex >= 29) { result.error = "Laengencode ausserhalb des Standards"; return result; }
                const size_t matchLength =
                    kLengthBase[lengthIndex] + in.Bits(kLengthExtra[lengthIndex]);

                const int distSymbol = dist->Decode(in);
                if (distSymbol < 0 || distSymbol >= 30 || in.Failed()) {
                    result.error = "ungueltiger Distanz-Code";
                    return result;
                }
                const size_t distance =
                    kDistBase[distSymbol] + in.Bits(kDistExtra[distSymbol]);
                if (distance > out.size()) {
                    result.error = "Rueckverweis zeigt vor den Anfang der Ausgabe";
                    return result;
                }

                // Byteweise kopieren, NICHT per memcpy: bei
                // distance < matchLength ueberlappen Quelle und
                // Ziel absichtlich (so kodiert DEFLATE Laeufe).
                size_t from = out.size() - distance;
                for (size_t i = 0; i < matchLength; ++i)
                    out.push_back(out[from + i]);
            }
        } else {
            result.error = "reservierter Blocktyp 3";
            return result;
        }

        if (final) break;
    }

    in.AlignToByte();
    result.consumed = in.BytePos();
    result.ok = true;
    return result;
}

InflateResult ZlibInflate(const uint8_t* data, size_t size,
                          size_t expectedSize, std::vector<uint8_t>& out) {
    InflateResult result;
    out.clear();
    if (size < 2) { result.error = "zu kurz fuer einen zlib-Kopf"; return result; }

    const uint8_t cmf = data[0];
    const uint8_t flg = data[1];
    if ((cmf & 0x0F) != 8) { result.error = "kein DEFLATE (Methode != 8)"; return result; }
    if (((static_cast<uint32_t>(cmf) << 8) | flg) % 31u != 0u) {
        result.error = "zlib-Kopfpruefsumme stimmt nicht";
        return result;
    }
    if (flg & 0x20) { result.error = "zlib-Stream mit Vorgabewoerterbuch wird nicht unterstuetzt"; return result; }

    InflateResult inner = RawInflate(data + 2, size - 2, expectedSize, out);
    if (!inner.ok) { result.error = inner.error; return result; }

    size_t consumed = 2 + inner.consumed;
    // Adler-32 pruefen, wenn er noch in der Eingabe liegt. Er ist
    // die einzige Stelle, an der ein Entpackfehler sich selbst
    // meldet - ohne ihn wuerde ein falscher Block als gueltige
    // Daten weitergereicht.
    if (consumed + 4 <= size) {
        const uint32_t stored =
            (static_cast<uint32_t>(data[consumed]) << 24) |
            (static_cast<uint32_t>(data[consumed + 1]) << 16) |
            (static_cast<uint32_t>(data[consumed + 2]) << 8) |
            static_cast<uint32_t>(data[consumed + 3]);
        const uint32_t computed = Adler32(out.data(), out.size());
        if (stored != computed) {
            char buf[128];
            std::snprintf(buf, sizeof buf,
                          "Adler-32 stimmt nicht: Datei %08x, berechnet %08x",
                          stored, computed);
            result.error = buf;
            out.clear();
            return result;
        }
        consumed += 4;
    }

    result.consumed = consumed;
    result.ok = true;
    return result;
}

} // namespace eg3d
