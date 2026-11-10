#include "barcodeGenerator.h"

#include <algorithm>
#include <cctype>

using namespace std;

// Standard Code 128 Set B pattern table (Indices 0 to 106)
string getCode128Pattern(int value) {
    static const string patterns[107] = {
        "11011001100", "11001101100", "11001100110", "10010011000", "10010001100", // 0-4
        "10001001100", "10011001000", "10011000100", "10001100100", "11001001000", // 5-9
        "11001000100", "11000100100", "10110011100", "10011011100", "10011001110", // 10-14
        "10111001100", "10011101100", "10011100110", "11001110010", "11001011100", // 15-19
        "11001001110", "11011100100", "11001110100", "11101101110", "11101001100", // 20-24
        "11100101100", "11100100110", "11101100100", "11100110100", "11100110010", // 25-29
        "11011011000", "11011000110", "11000110110", "10100011000", "10001011000", // 30-34
        "10001000110", "10110001000", "10001101000", "10001100010", "11010001000", // 35-39
        "11000101000", "11000100010", "10110111000", "10110001110", "10001101110", // 40-44
        "10111011000", "10111000110", "10001110110", "11101110110", "11010001110", // 45-49
        "11000101110", "11011101000", "11011100010", "11011101110", "11101011000", // 50-54
        "11101000110", "11100010110", "11101101000", "11101100010", "11100011010", // 55-59
        "11101111010", "11001000010", "11110001010", "10100110000", "10100001100", // 60-64
        "10010110000", "10010000110", "10000101100", "10000100110", "10110010000", // 65-69
        "10110000100", "10011010000", "10011000010", "10000110100", "10000110010", // 70-74
        "11000010010", "11001010000", "11110111010", "11000010100", "10001111010", // 75-79
        "10100111100", "10010111100", "10010011110", "10111100100", "10011110100", // 80-84
        "10011110010", "11110100100", "11110010100", "11110010010", "11011011110", // 85-89
        "11011110110", "11110110110", "10101111000", "10100011110", "10001011110", // 90-94
        "10111101000", "10111100010", "11110101000", "11110100010", "10111011110", // 95-99
        "10111101110", "11101011110", "11110101110", "11010000100", "11010010000", // 100-104 (104: Start B)
        "11010011100",                                                                // 105: Start C
        "1100011101011"                                                               // 106: Stop (13 bits)
    };

    if (value >= 0 && value <= 106) {
        return patterns[value];
    }
    return "";
}

// 1. Input Validation: Check 6 digits and first digit != '0'
bool isValid6DigitInput(const string& s) {
    return s.size() == 6 &&
        s[0] != '0' &&
        all_of(s.begin(), s.end(),
            [](unsigned char c) { return isdigit(c); });
}

// 2. Modulo 103 Checksum Calculation
int calculateChecksum(int startValue, const string& code6) {
    int sum = startValue;
    for (size_t i = 0; i < code6.length(); ++i) {
        int charValue = static_cast<int>(code6[i]) - 32;
        int weight = static_cast<int>(i) + 1; // Position index: 1, 2, 3, 4, 5, 6
        sum += (charValue * weight);
    }
    return sum % 103;
}

string generateBarcodeBits(const string& code6) {
    const int START_B = 104;
    const int STOP_CODE = 106;
    const int QUIET = 10;

    if (!isValid6DigitInput(code6)) {
        return "";
    }

    string bits;
    bits.reserve(121);                      // memory ek hi baar allocate

    bits.append(QUIET, '0');                // left quiet zone
    bits += getCode128Pattern(START_B);

    for (char c : code6) {
        bits += getCode128Pattern(c - 32);  // Set B value
    }

    bits += getCode128Pattern(calculateChecksum(START_B, code6));
    bits += getCode128Pattern(STOP_CODE);
    bits.append(QUIET, '0');                // right quiet zone

    return bits;                            // hamesha 121 bits
}

// 4. Strict Multi-Point Verification
bool verifyEncoding(const string& code6, const string& generatedBits) {
    // Total bits check: 10 + 11 + (6 * 11) + 11 + 13 + 10 = 121 bits
    if (generatedBits.length() != 121) {
        return false;
    }

    // Quiet Zones check
    if (generatedBits.substr(0, 10) != "0000000000") {
        return false;
    }
    if (generatedBits.substr(111, 10) != "0000000000") {
        return false;
    }

    // Start B check (bits 10 to 20)
    if (generatedBits.substr(10, 11) != getCode128Pattern(104)) {
        return false;
    }

    // 6 Data Digits check (each 11 bits)
    size_t offset = 21;
    for (char c : code6) {
        int val = static_cast<int>(c) - 32;
        if (generatedBits.substr(offset, 11) != getCode128Pattern(val)) {
            return false;
        }
        offset += 11;
    }

    // Checksum check (bits 87 to 97)
    int expectedChecksum = calculateChecksum(104, code6);
    if (generatedBits.substr(87, 11) != getCode128Pattern(expectedChecksum)) {
        return false;
    }

    // Stop pattern check (bits 98 to 110, length 13)
    if (generatedBits.substr(98, 13) != getCode128Pattern(106)) {
        return false;
    }

    return true;
}