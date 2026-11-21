#ifndef BARCODE_GENERATOR_H
#define BARCODE_GENERATOR_H

#include <string>
using namespace std;

// 1. Input Validation: Check karega ke input exactly 6 digits ka ho aur pehla digit '1'-'9' ho
bool isValid6DigitInput(const string& input);

// 2. Lookup standard 11-bit pattern for Code 128 (Set B) values (0 se 106 tak)
string getCode128Pattern(int value);

// 3. Modulo 103 checksum calculate karega
int calculateChecksum(int startValue, const string& code6);

// 4. Poori 121-bit binary string (Quiet Zones + Start B + 6 Digits + Checksum + Stop + Quiet Zones) banayega
string generateBarcodeBits(const string& code6);

// 5. Generated bitstream ko verify karega (quiet zones, start marker, data, checksum, aur stop pattern)
bool verifyEncoding(const string& code6, const string& generatedBits);

#endif // BARCODE_GENERATOR_H