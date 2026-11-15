#pragma once
// FM SYSEX — reading 6-operator voice data in the DX7 SysEx format (TrackWerk, Kid Bunka, GPL-3.0).
//
// Two message kinds are accepted, exactly as the format defines them:
//   32-voice bank   F0 43 0n 09 20 00  <4096 bytes: 32 × 128 packed voices>  checksum  F7   (4104 bytes)
//   single voice    F0 43 0n 00 01 1B  <155 bytes: one unpacked voice>         checksum  F7   (163 bytes)
// n is the device number (0…15). Every data byte must be 7-bit. The checksum is the two's complement
// of the data's sum, 7 bits; a wrong checksum is reported but does not reject the data, since many
// archived banks carry one — the voice parameters are range-checked again when a voice is unpacked
// (plaits::fm::Patch::Unpack clamps every field). A file may hold other bytes before the message; the
// first well-formed message is used. A single voice is packed into the 128-byte bank layout, so the
// rest of the app only ever handles packed voices.
//
// Pure functions: no allocation, no I/O, nothing shared — safe on any thread.
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace studio::fm_sysex {

inline constexpr int kPackedSize = 128;
inline constexpr int kVoicesPerBank = 32;
inline constexpr int kBankDataSize = kPackedSize * kVoicesPerBank;   // 4096
inline constexpr int kBankMessageSize = 6 + kBankDataSize + 2;          // 4104
inline constexpr int kVoiceDataSize = 155;
inline constexpr int kVoiceMessageSize = 6 + kVoiceDataSize + 2;        // 163
inline constexpr size_t kMaxFileSize = 64 * 1024;                        // larger files are not banks

using PackedVoice = std::array<uint8_t, kPackedSize>;

struct Bank {
    std::array<PackedVoice, kVoicesPerBank> voices{};
    int count = 0;              // 32 for a bank, 1 for a single voice, 0 = nothing usable found
    bool checksumOk = false;
};

inline uint8_t checksum(const uint8_t* data, size_t size) {
    unsigned sum = 0;
    for (size_t i = 0; i < size; ++i) sum += data[i];
    return uint8_t((128u - (sum & 127u)) & 127u);
}

inline bool seven_bit(const uint8_t* data, size_t size) {
    for (size_t i = 0; i < size; ++i)
        if (data[i] & 0x80u) return false;
    return true;
}

// One unpacked voice (155 bytes, VCED order: operators 6…1, then the global parameters) into the
// packed 128-byte layout of a bank (VMEM).
inline PackedVoice pack_voice(const uint8_t* v) {
    PackedVoice p{};
    for (int op = 0; op < 6; ++op) {
        const uint8_t* s = v + op * 21;
        uint8_t* d = p.data() + op * 17;
        for (int i = 0; i < 11; ++i) d[i] = s[i];                    // rates, levels, break point, depths
        d[11] = uint8_t((s[11] & 3) | ((s[12] & 3) << 2));          // left / right curve
        d[12] = uint8_t((s[13] & 7) | ((s[20] & 15) << 3));         // rate scaling, detune
        d[13] = uint8_t((s[14] & 3) | ((s[15] & 7) << 2));          // amp mod sens., velocity sens.
        d[14] = s[16];                                              // output level
        d[15] = uint8_t((s[17] & 1) | ((s[18] & 31) << 1));         // oscillator mode, coarse
        d[16] = s[19];                                              // fine
    }
    const uint8_t* g = v + 126;
    for (int i = 0; i < 8; ++i) p[size_t(102 + i)] = g[i];           // pitch envelope rates, levels
    p[110] = uint8_t(g[8] & 31);                                    // algorithm
    p[111] = uint8_t((g[9] & 7) | ((g[10] & 1) << 3));              // feedback, oscillator key sync
    p[112] = g[11];                                                 // LFO speed
    p[113] = g[12];                                                 // LFO delay
    p[114] = g[13];                                                 // pitch mod depth
    p[115] = g[14];                                                 // amp mod depth
    p[116] = uint8_t((g[15] & 1) | ((g[16] & 7) << 1) | ((g[17] & 7) << 4));   // sync, wave, pitch mod sens.
    p[117] = g[18];                                                 // transpose
    for (int i = 0; i < 10; ++i) p[size_t(118 + i)] = g[19 + i];    // name
    return p;
}

// The first well-formed bank or single-voice message in `data`.
inline Bank parse(const uint8_t* data, size_t size) {
    Bank bank;
    if (!data || size > kMaxFileSize) return bank;
    for (size_t at = 0; at + kVoiceMessageSize <= size; ++at) {
        const uint8_t* m = data + at;
        if (m[0] != 0xF0 || m[1] != 0x43 || (m[2] & 0xF0) != 0x00) continue;
        if (m[3] == 0x09 && m[4] == 0x20 && m[5] == 0x00 && at + kBankMessageSize <= size &&
            m[kBankMessageSize - 1] == 0xF7 && seven_bit(m + 6, kBankDataSize + 1)) {
            for (int v = 0; v < kVoicesPerBank; ++v)
                for (int i = 0; i < kPackedSize; ++i) bank.voices[size_t(v)][size_t(i)] = m[6 + v * kPackedSize + i];
            bank.count = kVoicesPerBank;
            bank.checksumOk = checksum(m + 6, kBankDataSize) == m[6 + kBankDataSize];
            return bank;
        }
        if (m[3] == 0x00 && m[4] == 0x01 && m[5] == 0x1B && m[kVoiceMessageSize - 1] == 0xF7 &&
            seven_bit(m + 6, kVoiceDataSize + 1)) {
            bank.voices[0] = pack_voice(m + 6);
            bank.count = 1;
            bank.checksumOk = checksum(m + 6, kVoiceDataSize) == m[6 + kVoiceDataSize];
            return bank;
        }
    }
    return bank;
}

// The voice's ten-character name, trailing spaces removed; anything not printable ASCII becomes '?'.
inline std::string voice_name(const PackedVoice& v) {
    std::string name;
    for (int i = 0; i < 10; ++i) {
        const char c = char(v[size_t(118 + i)]);
        name += (c >= 32 && c < 127) ? c : '?';
    }
    while (!name.empty() && name.back() == ' ') name.pop_back();
    return name.empty() ? std::string("(no name)") : name;
}

// 256 lowercase hex digits, for the project file.
inline std::string to_hex(const PackedVoice& v) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(kPackedSize * 2);
    for (uint8_t b : v) {
        out += digits[b >> 4];
        out += digits[b & 15];
    }
    return out;
}
// False, `out` untouched, unless `hex` is exactly 256 hex digits of 7-bit bytes.
inline bool from_hex(const std::string& hex, PackedVoice& out) {
    if (hex.size() != size_t(kPackedSize) * 2) return false;
    const auto nibble = [](char c) {
        return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
    };
    PackedVoice v{};
    for (int i = 0; i < kPackedSize; ++i) {
        const int hi = nibble(hex[size_t(2 * i)]), lo = nibble(hex[size_t(2 * i + 1)]);
        if (hi < 0 || lo < 0 || hi > 7) return false;
        v[size_t(i)] = uint8_t(hi << 4 | lo);
    }
    out = v;
    return true;
}

}  // namespace studio::fm_sysex
