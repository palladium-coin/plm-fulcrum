//
// Fulcrum - A fast & nimble SPV Server for Bitcoin Cash
// Copyright (C) 2019-2026 Calin A. Culianu <calin.culianu@gmail.com>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program (see LICENSE.txt).  If not, see
// <https://www.gnu.org/licenses/>.
//
#pragma once

#include "BTC.h"
#include "Compat.h"

#include "bitcoin/uint256.h"

#include <QByteArray>
#include <QMetaType>
#include <QString>

#include <compare>
#include <cstring>
#include <optional>
#include <tuple>
#include <vector>

namespace BTC {
    struct Address
    {
        static constexpr std::size_t H160Len = bitcoin::uint160::size();
        static constexpr std::size_t H256Len = bitcoin::uint256::size();

        enum Kind : Byte {
            Invalid = 0xff,
            P2PKH = 0,  ///< base58
            P2SH = 1,   ///< base58
            // bech32 / bech32m segwit addresses
            P2WPKH = 2, ///< witness v0, 20 byte program
            P2WSH = 3,  ///< witness v0, 32 byte program
            P2TR = 4,   ///< witness v1 (taproot), 32 byte program
        };

        static constexpr bool isWitnessKind(Kind k) noexcept { return k == P2WPKH || k == P2WSH || k == P2TR; }

        static constexpr Byte InvalidVerByte = 0xff;

        Address() noexcept = default;
        /// for the below 3 c'tors, the 'net' is auto-detected based on address.
        Address(const QString &address) { *this = Address::fromString(address); }
        Address(const char *address) { *this = address; }
        Address(const QByteArray &address) { *this = address; }

        /// Auto-detects Net based on decoded address contents. The string is parsed as a PLM base58 (P2PKH, P2SH) or
        /// bech32/bech32m (P2WPKH, P2WSH, P2TR) address.
        static Address fromString(const QString &address);
        /// Note that for fromPubKey, kind must be P2PKH
        static Address fromPubKey(const Byte *pbegin, const Byte *pend, Kind, Net = MainNet);
        static Address fromPubKey(const QByteArray &pubKey, Kind kind, Net net = MainNet) { return fromPubKey(reinterpret_cast<const Byte *>(pubKey.constData()), reinterpret_cast<const Byte *>(pubKey.constData() + pubKey.length()), kind, net); }
        static Address fromPubKey(const std::vector<Byte> &pubKey, Kind kind, Net net = MainNet) { return fromPubKey(&*pubKey.begin(), &*pubKey.end(), kind, net); }

        static Address fromP2SHScriptHash(const ByteView &sh, Net net = MainNet);

        const QByteArray & hash() const noexcept { return _hash; }

        Kind kind() const noexcept { return _kind; }
        Net net() const noexcept { return _net; }

        //! Returns true if this address is valid would be identical if encoded on network `net`.
        //! This is only true for TestNet & RegTestNet base58 addresses which are interchangeable.
        bool isCompatibleWithNet(Net net) const;

        bool isTestNet() const noexcept { return _net == TestNet; }
        bool isRegTestNet() const noexcept { return _net == RegTestNet; }
        bool isMainNet() const noexcept { return _net == MainNet; }

        bool isValid() const noexcept {
            return _kind != Kind::Invalid && (_hash.length() == H160Len || _hash.length() == H256Len)
                    && _net != Net::Invalid && verByte != InvalidVerByte;
        }

        /// test any string to see if it's a valid address for the specified network
        static bool isValid(const QString &address, Net = MainNet);

        /// Returns the bitcoin script bytes as would be used in a spending transaction.
        /// for use with CTransaction et al in the txOut )
        bitcoin::CScript toCScript() const;
        /// Returns the bitcoin script bytes as would be used in a spending transaction,
        /// hashed once with sha256, as raw bytes. (in reversed memory order, as EX/Fulcrum would use for its HashX)
        QByteArray toHashX() const;

        /// If isValid, returns the address as a string: P2PKH & P2SH are base58, the witness kinds are bech32/bech32m.
        /// Returns an empty QString if !isValid.
        QString toString() const;

        Address & operator=(const QString &address) { return (*this = Address::fromString(address)); }
        Address & operator=(const char *address) { return (*this = QString(address)); }
        Address & operator=(const QByteArray &address) { return (*this = QString(address)); }

        /// Comparison operator: for map support and also so that it sorts like the text address would.
        /// All invalid addresses sort before valid ones.
        std::strong_ordering operator<=>(const Address & o) const noexcept {
            if (isValid() && o.isValid())
                // lex-compare using 3-way tuple compare
                return std::tie(_net, verByte, _kind, _hash) <=> std::tie(o._net, o.verByte, o._kind, o._hash);
            else
                // invalid always sorts before valid; both invalid are equal
                return isValid() - o.isValid() <=> 0;
        }

        bool operator==(const Address & o) const noexcept { return this->operator<=>(o) == 0; }

    private:
        Net _net = BTC::Invalid;
        Byte verByte = InvalidVerByte;
        QByteArray _hash; // this holds either a p2pkh or p2sh payload, or a witness program
        Kind _kind = Kind::Invalid;
        bool autosetKind();

#ifdef ENABLE_TESTS
    public:
        static bool test();
        static void bench();
#endif
    };

} // end namespace BTC

/// for std::hash support of type BTC::Address -- just take middle 4 or 8 bytes of a.hash()
template <> struct std::hash<BTC::Address> {
    std::size_t operator()(const BTC::Address &a) const noexcept {
        if (a.isValid()) {
            // The below will produce a good value because isValid implies hash() length is 20 or 32
            return BTC::QByteArrayHashHasher{}(a.hash());
        }
        // invalid will always hash to 0
        return 0;
    }
};

namespace BTC {
    /// for Qt QSet/QHash support of type BTC::Address (must be in BTC namespace for ADL lookup to work)
    inline Compat::qhuint qHash(const Address &key, Compat::qhuint seed = 0) {
        return ::qHash(std::hash<Address>{}(key), seed);
    }
} // namespace BTC

Q_DECLARE_METATYPE(BTC::Address);
