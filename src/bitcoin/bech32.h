// Copyright (c) 2017 Pieter Wuille
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Bech32 is a string encoding format used in newer address types.
// The output consists of a human-readable part (alphanumeric), a
// separator character (1), and a base32 data section, the last
// 6 characters of which are a checksum.
//
// For more information, see BIP 173.

#ifndef FULCRUM_BITCOIN_BECH32_H
#define FULCRUM_BITCOIN_BECH32_H

// Taken from Bitcoin Core / Palladium Core (MIT), trimmed to the Encode/Decode used by BTC::Address.

#include <stdint.h>
#include <string>
#include <vector>

namespace bech32
{

/** The Bech32 and Bech32m checksum encodings. */
enum class Encoding {
    INVALID,
    BECH32,
    BECH32M,
};

/** Encode a Bech32 or Bech32m string. If hrp contains uppercase characters, this will cause an assertion error. */
std::string Encode(Encoding encoding, const std::string& hrp, const std::vector<uint8_t>& values);

/** Decode a Bech32 or Bech32m string, returning the checksum encoding used. */
Encoding Decode(const std::string& str, std::string& out_hrp, std::vector<uint8_t>& out_values);

} // namespace bech32

#endif // FULCRUM_BITCOIN_BECH32_H
