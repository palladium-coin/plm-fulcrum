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
#include "BTC.h"
#include "BTC_Address.h"
#include "Common.h"
#include "Util.h"

#include "bitcoin/base58.h"
#include "bitcoin/bech32.h"
#include "bitcoin/cashaddrenc.h"
#include "bitcoin/script.h"

#include <QHash>
#include <QHashFunctions>

#include <mutex>
#include <span>
#include <utility>

#ifdef ENABLE_TESTS
#  include "App.h"
#  include "Compat.h"
#  include "Json/Json.h"
#  include "bitcoin/cashaddr.h"
#  include <QRandomGenerator>
#  include <iostream>
#  include <thread>
#  include <utility>
#endif

namespace BTC
{
    inline Compat::qhuint qHash(BTC::Net n, Compat::qhuint seed = 0) noexcept { return ::qHash(uchar(n), seed); }

    namespace {
        // Hash of Net -> [Hash of VerByte -> Kind]
        const QHash<Net, QHash<quint8, Address::Kind> > netVerByteKindMap = {
            { MainNet,      { {0, Address::P2PKH }, {5, Address::P2SH} } },
            { TestNet,    { {111, Address::P2PKH }, {196, Address::P2SH} } },
            { TestNet4,   { {111, Address::P2PKH }, {196, Address::P2SH} } },
            { ScaleNet,   { {111, Address::P2PKH }, {196, Address::P2SH} } },
            { ChipNet,    { {111, Address::P2PKH }, {196, Address::P2SH} } },
            { RegTestNet, { {111, Address::P2PKH }, {196, Address::P2SH} } },
        };
        // PLM (Palladium) base58 version bytes; same values as palladiumcore's chainparams.cpp. Regtest shares testnet's.
        const QHash<Net, QHash<quint8, Address::Kind> > plmNetVerByteKindMap = {
            { MainNet,      { {55, Address::P2PKH }, {5, Address::P2SH} } },
            { TestNet,    { {127, Address::P2PKH }, {115, Address::P2SH} } },
            { RegTestNet, { {127, Address::P2PKH }, {115, Address::P2SH} } },
        };
        /// PLM bech32 human-readable part for the net, or nullptr if the net has no PLM equivalent.
        const char *plmHrpForNet(Net net) {
            switch (net) {
            case MainNet: return "plm";
            case TestNet: return "tplm";
            case RegTestNet: return "rplm";
            default: return nullptr;
            }
        }
        const QHash<Net, QHash<quint8, Address::Kind> > & verByteKindMapFor(Coin coin) {
            return coin == Coin::PLM ? plmNetVerByteKindMap : netVerByteKindMap;
        }
        /// Regroup bits (BIP173 reference ConvertBits). Returns false on invalid padding.
        template <int From, int To, bool Pad>
        bool convertBits(std::vector<Byte> &out, const Byte *begin, const Byte *end) {
            unsigned acc = 0;
            int bits = 0;
            constexpr unsigned maxv = (1u << To) - 1u, maxAcc = (1u << (From + To - 1)) - 1u;
            for (const Byte *it = begin; it != end; ++it) {
                acc = ((acc << From) | *it) & maxAcc;
                bits += From;
                while (bits >= To) {
                    bits -= To;
                    out.push_back(Byte((acc >> bits) & maxv));
                }
            }
            if constexpr (Pad) {
                if (bits) out.push_back(Byte((acc << (To - bits)) & maxv));
            } else if (bits >= From || ((acc << (To - bits)) & maxv)) {
                return false;
            }
            return true;
        }
        Byte verByteForNetAndKind(Coin coin, Net net, Address::Kind kind) {
            if (coin == Coin::PLM && Address::isWitnessKind(kind))
                // for witness kinds "verByte" holds the witness version
                return plmHrpForNet(net) ? (kind == Address::P2TR ? 1 : 0) : Address::InvalidVerByte;
            if (kind == Address::TOKEN_P2PKH) kind = Address::P2PKH; // hack to support TOKEN_P2PKH
            else if (kind == Address::TOKEN_P2SH) kind = Address::P2SH; // hack to support TOKEN_P2SH
            if (const auto map = verByteKindMapFor(coin).value(net); !map.isEmpty()) [[likely]] {
                for (auto it = map.begin(); it != map.end(); ++it) {
                    if (it.value() == kind)
                        return it.key();
                }
            }
            return Address::InvalidVerByte;
        }
        Address::Kind kindForNetAndVerByte(Coin coin, Net net, Byte verByte) {
            if (const auto map = verByteKindMapFor(coin).value(net); !map.isEmpty()) [[likely]] {
                for (auto it = map.begin(); it != map.end(); ++it) {
                    if (it.key() == verByte)
                        return it.value();
                }
            }
            return Address::Kind::Invalid;
        }
        /// NB: This won't ever auto-detect regtest since it has the same verBytes as testnet (111 & 196).  Since
        /// between the two choices, testnet is the much more likely network anybody using this software will be using,
        /// we always return testnet if the verByte matches regtest and/or testnet.
        Net netForVerByte(Coin coin, Byte verByte) {
            const auto & verByteKindMap = verByteKindMapFor(coin);
            for (auto it = verByteKindMap.begin(); it != verByteKindMap.end(); ++it) {
                const Net net = it.key();
                if (net == RegTestNet)
                    // don't auto-detect regtest net -- skip it.
                    continue;
                if (net == TestNet4)
                    // don't auto-detect testnet4 for now -- skip it.
                    continue;
                if (net == ScaleNet)
                    // don't auto-detect scalenet for now -- skip it.
                    continue;
                if (net == ChipNet)
                    // don't auto-detect chipnet for now -- skip it.
                    continue;
                if (it.value().contains(verByte))
                    return net;
            }
            return Net::Invalid;
        }
    } // end anon namespace

    /// -- Address --

    bool Address::isCompatibleWithNet(Net net) const
    {
        if (_net == Net::Invalid || net == Net::Invalid)
            return false;
        if (net == _net)
            return true;
        Address other(*this);
        other._net = net;
        other.verByte = verByteForNetAndKind(other._coin, other._net, other._kind);
        // true if both cashaddr and legacy encodings match
        return other.isValid()
                && toString(false) == other.toString(false)
                && toString(true) == other.toString(true);
    }

    /*static*/
    Address Address::fromString(const QString &legacyOrCash, Coin coin)
    {
        static const auto DecodeCash = [] (Address & a, const std::string &ss) -> bool {
            using PN = std::pair<const std::string &, const Net>;
            a._net = Net::Invalid;
            bitcoin::CashAddrContent content;
            // Keep trying to decode with the various prefixes until we get a match.
            // Note that testnet4 and testnet have the same prefix, but we added both to this loop,
            // just in case that situation changes.
            for (const auto & [prefix, net] : {
                    PN{bitcoin::MainNetChainParams.CashAddrPrefix(), Net::MainNet},
                    PN{bitcoin::TestNetChainParams.CashAddrPrefix(), Net::TestNet},
                    PN{bitcoin::TestNet4ChainParams.CashAddrPrefix(), Net::TestNet4},
                    PN{bitcoin::ScaleNetChainParams.CashAddrPrefix(), Net::ScaleNet},
                    PN{bitcoin::ChipNetChainParams.CashAddrPrefix(), Net::ChipNet},
                    PN{bitcoin::RegTestNetChainParams.CashAddrPrefix(), Net::RegTestNet},})
            {
                content = bitcoin::DecodeCashAddrContent(ss, prefix);
                if (!content.hash.empty()) {
                    a._net = net;
                    break;
                }
            }
            if (!content.hash.empty() && a._net != Net::Invalid) {
                a._kind = static_cast<Address::Kind>(content.type) /* Our Kind enum intentionally matches content.type's enum for values, so this works*/;
                a.verByte = verByteForNetAndKind(Coin::Unknown, a._net, a._kind);
                if (a.verByte == InvalidVerByte) [[unlikely]]
                    // Defensive programming.. we should never reach this branch.
                    throw Exception("Unknown content.type or other missing data on cash addr decode attempt");
                a._hash.clear();
                a._hash.insert(0, reinterpret_cast<const char *>(content.hash.data()), int(content.hash.size()));
                return a.isValid(); // this is here as a reduntant check -- this should always be true if we get here
            }
            return false;
        };
        static const auto DecodePLMWitness = [] (Address & a, const std::string &ss) -> bool {
            std::string hrp;
            std::vector<Byte> data;
            const auto enc = bech32::Decode(ss, hrp, data);
            if (enc == bech32::Encoding::INVALID || data.empty() || data[0] > 16)
                return false;
            Net net = Net::Invalid;
            for (const Net n : {MainNet, TestNet, RegTestNet}) {
                if (hrp == plmHrpForNet(n)) { net = n; break; }
            }
            if (net == Net::Invalid)
                return false;
            const Byte witver = data[0];
            std::vector<Byte> prog;
            if (!convertBits<5, 8, false>(prog, data.data() + 1, data.data() + data.size()))
                return false;
            // Only the 3 standard output types are supported. Witness v0 must be bech32, v1+ must be bech32m (BIP350).
            Address::Kind kind = Address::Invalid;
            if (witver == 0 && enc == bech32::Encoding::BECH32) {
                if (prog.size() == Address::H160Len) kind = Address::P2WPKH;
                else if (prog.size() == Address::H256Len) kind = Address::P2WSH;
            } else if (witver == 1 && enc == bech32::Encoding::BECH32M && prog.size() == Address::H256Len) {
                kind = Address::P2TR;
            }
            if (kind == Address::Invalid)
                return false;
            a._coin = Coin::PLM;
            a._net = net;
            a._kind = kind;
            a.verByte = witver;
            a._hash = QByteArray(reinterpret_cast<const char *>(prog.data()), int(prog.size()));
            return true;
        };
        Address a;
        std::vector<Byte> dec;
        if (const auto ss = legacyOrCash.toStdString(); bitcoin::DecodeBase58Check(ss, dec)) {
            // Legacy address
            if (const auto decsize = dec.size(); !(decsize == 1 + H160Len || decsize == 1 + H256Len)) {
#ifdef QT_DEBUG
                DebugM(__func__, ": bad decoded length  ", dec.size(), " for address ", legacyOrCash);
#endif
            } else {
                a.verByte = dec[0];
                a._hash.resize(decsize - 1);
                std::memcpy(a._hash.data(), &dec[1], a._hash.size());
                a._coin = coin;
                a._net = netForVerByte(coin, a.verByte); // note this will not correctly differntiate between TestNet, TestNet4, ScaleNet and RegTestNet and always pick TestNet since they have the same verBytes.
                a.autosetKind(); // this will clear the address if something is wrong
            }
        } else {
            // Cash address
            bool ok = false;
            try {
                if (!(ok = (coin == Coin::PLM ? DecodePLMWitness(a, ss) : DecodeCash(a, ss)))) {
#ifdef QT_DEBUG
                    DebugM(__func__, ": got bad address ", legacyOrCash);
#endif
                }
            } catch (const std::exception &e) {
                Warning() << "Internal error decoding cash address " << legacyOrCash << ": " << e.what();
            }
            if (!ok) a = Address(); // clear it to save memory.
        }
        // if either of the above branches succeeded, a.isValid(), otherwise it will just be a default-constructed address.
        return a;
    }

    /* static */
    Address Address::fromPubKey(const Byte *pbegin, const Byte *pend, Kind k, Net net)
    {
        Address ret;
        if (k == Kind::P2PKH || k == Kind::TOKEN_P2PKH) [[likely]] {
            if (const auto vb = verByteForNetAndKind(Coin::Unknown, net, k); vb != InvalidVerByte && pend > pbegin) [[likely]] {
                const auto hash160 = bitcoin::Hash160(pbegin, pend);
                ret._hash = QByteArray(reinterpret_cast<const char *>(hash160.begin()), int(hash160.size()));
                ret.verByte = vb;
                ret._kind = k;
                ret._net = net;
            }
        }
        return ret;
    }

    /* static */
    Address Address::fromP2SHScriptHash(const ByteView &sh, Net net)
    {
        Address ret;
        ret._kind = Kind::P2SH;
        ret._hash = sh.toByteArray(true);
        ret._net = net;
        ret.verByte = verByteForNetAndKind(Coin::Unknown, ret._net, ret._kind);
        return ret;
    }


    // Used internally in one branch when parsing Legacy.  If it fails it will clear the address to default constructed values.
    bool Address::autosetKind()
    {
        if (const auto len = _hash.length(); len == int(H160Len)) {
            _kind = kindForNetAndVerByte(_coin, _net, verByte);
            if (_kind != Kind::Invalid)
                return true;
        } else if (len == int(H256Len) && _coin != Coin::PLM /* PLM has no p2sh32 */) {
            _kind = kindForNetAndVerByte(_coin, _net, verByte);
            if (_kind == Kind::P2SH || _kind == Kind::TOKEN_P2SH)
                return true;
        }
        // if we get here, there was a problem, so clear to default and return false.
        *this = Address();
        return false;
    }

    bitcoin::CScript Address::toCScript() const
    {
        using namespace bitcoin;
        CScript ret;
        const auto hbytes = std::as_bytes(std::span{_hash});
        if (const size_t len = _hash.length(); _kind == P2WPKH && len == H160Len) {
            ret << OP_0 << hbytes;
        } else if ((_kind == P2WSH) && len == H256Len) {
            ret << OP_0 << hbytes;
        } else if (_kind == P2TR && len == H256Len) {
            ret << OP_1 << hbytes;
        } else if (len == H160Len) {
            if (_kind == P2PKH || _kind == TOKEN_P2PKH) {
                ret << OP_DUP << OP_HASH160 << hbytes << OP_EQUALVERIFY << OP_CHECKSIG;
            } else if (_kind == P2SH || _kind == TOKEN_P2SH) {
                ret << OP_HASH160 << hbytes << OP_EQUAL;
            }
        } else if (len == H256Len && (_kind == P2SH || _kind == TOKEN_P2SH)) {
            ret.reserve(len + 3u); // long script exceeds the 28-byte static_capacity of CScript, so pre-reserve.
            ret << OP_HASH256 << hbytes << OP_EQUAL;
        }
        return ret;
    }

    QByteArray Address::toHashX() const
    {
        const auto cscript = toCScript();
        if (cscript.empty())
            return QByteArray();
        return BTC::HashXFromCScript( cscript );
    }

    /// if isValid:  Returns the legacy address string, base58 encoded if legacy==true,
    ///              otherwise returns the cash address with prefix
    /// if !isValid: Returns the empty string.
    QString Address::toString(bool legacy, std::optional<Byte> verByteOverride) const {
        QString ret;
        if (isValid()) {
            if (_coin == Coin::PLM && isWitnessKind(_kind)) {
                if (const char *hrp = plmHrpForNet(_net); hrp) {
                    std::vector<Byte> data{verByte};
                    convertBits<8, 5, true>(data, reinterpret_cast<const Byte *>(_hash.constData()),
                                            reinterpret_cast<const Byte *>(_hash.constData()) + _hash.size());
                    ret = QString::fromStdString(bech32::Encode(_kind == P2TR ? bech32::Encoding::BECH32M
                                                                              : bech32::Encoding::BECH32, hrp, data));
                }
            } else if (legacy || _coin == Coin::PLM /* PLM has no cashaddr */) {
                std::vector<Byte> vch;
                vch.reserve(1 + size_t(_hash.size()));
                vch.push_back(verByteOverride.value_or(verByte));
                vch.insert(vch.end(), _hash.begin(), _hash.end());
                ret = QString::fromStdString(bitcoin::EncodeBase58Check(vch));
            } else {
                const std::string *prefix = nullptr;
                switch (_net) {
                case Net::MainNet:    prefix = &bitcoin::MainNetChainParams.cashaddrPrefix; break;
                case Net::TestNet:    prefix = &bitcoin::TestNetChainParams.cashaddrPrefix; break;
                case Net::TestNet4:   prefix = &bitcoin::TestNet4ChainParams.cashaddrPrefix; break;
                case Net::ScaleNet:   prefix = &bitcoin::ScaleNetChainParams.cashaddrPrefix; break;
                case Net::ChipNet:    prefix = &bitcoin::ChipNetChainParams.cashaddrPrefix; break;
                case Net::RegTestNet: prefix = &bitcoin::RegTestNetChainParams.cashaddrPrefix; break;
                case Net::Invalid:    break;
                }
                if (prefix) {
                    const std::vector<Byte> content(_hash.begin(), _hash.end());
                    const auto type = static_cast<bitcoin::CashAddrType>(_kind);
                    ret = QString::fromStdString( bitcoin::EncodeCashAddr(*prefix, { type, content }) );
                }
            }
        }
        return ret;
    }

    QString Address::toShortString() const
    {
        QString s = toString(false);
        if (int colon = s.indexOf(':'); colon > -1)
            s = s.mid(colon+1);
        else
            s.clear();
        return s;
    }

    /*static*/
    bool Address::isValid(const QString &legacyOrCashAddress, Net net, Coin coin)
    {
        const Address a = Address::fromString(legacyOrCashAddress, coin);
        return a.isValid() && a._net == net;
    }

#ifdef ENABLE_TESTS
    namespace {
        struct ThreadSafeLogger {
            QString str;
            QTextStream ts{&str};
            static std::mutex mut;

            ~ThreadSafeLogger() {
                ts.flush();
                std::unique_lock g(mut);
                std::cout << str.toUtf8().constData() << "\n";
            }
            template <typename T>
            ThreadSafeLogger &operator<<(T &&t) { ts << t; return *this; }
        };
        std::mutex ThreadSafeLogger::mut;
    }
    /*static*/
    void Address::bench()
    {
        using Print = ThreadSafeLogger;
        std::condition_variable cond;
        std::mutex mut;
        std::atomic_bool start{false};

        const auto Bench = [&cond, &mut, &start](size_t id){
            while (!start) {
                std::unique_lock g(mut);
                if (!start)
                    cond.wait(g);
            }
            constexpr auto MyNet = Net::MainNet;
            constexpr auto MyKind = Kind::P2PKH;
            constexpr size_t count = 300'000;
            Print() << id << ": Generating " << count << " random pubkeys...";
            std::vector<QByteArray> pubkeys(count);
            const auto t0pk = Util::getTimeNS();
            for (size_t i = 0; i < count; ++i) {
                QByteArray & pk = pubkeys[i];
                pk.resize(int(sizeof(quint32)*4));
                QRandomGenerator::global()->fillRange(reinterpret_cast<quint32 *>(pk.data()), pk.size()/int(sizeof(quint32)));
            }
            const auto elapsedpk = Util::getTimeNS() - t0pk;
            Print() << id << ": Took: " << QString::number(elapsedpk/1e6, 'f', 6).toUtf8().constData() << " msec";

            Print() << id << ": Generating " << count << " legacy address strings ...";
            std::vector<QString> legStrings(count);
            const auto t0ls = Util::getTimeNS();
            for (size_t i = 0; i < count; ++i) {
                QByteArray & pk = pubkeys[i];
                QString & leg = legStrings[i];

                const auto a = Address::fromPubKey(pk, MyKind, MyNet);
                leg = a.toLegacyString();
            }
            const auto elapsedls = Util::getTimeNS() - t0ls;
            Print() << id << ": Took: " << QString::number(elapsedls/1e6, 'f', 6).toUtf8().constData() << " msec";
            Print() << id << ": Last string: " << legStrings.back().toUtf8().constData();

            Print() << id << ": Generating " << count << " cash address strings ...";
            std::vector<QString> caStrings(count);
            const auto t0ca = Util::getTimeNS();
            for (size_t i = 0; i < count; ++i) {
                QByteArray & pk = pubkeys[i];
                QString & ca = caStrings[i];

                const auto a = Address::fromPubKey(pk, MyKind, MyNet);
                ca = a.toString();
            }
            const auto elapsedca = Util::getTimeNS() - t0ca;
            Print() << id << ": Took: " << QString::number(elapsedca/1e6, 'f', 6).toUtf8().constData() << " msec";
            Print() << id << ": Last string: " << caStrings.back().toUtf8().constData();

            Print() << id << ": Parsing " << count << " legacy strings ...";
            std::vector<Address> addrsleg(count);
            const auto t0pleg = Util::getTimeNS();
            for (size_t i = 0; i < count; ++i) {
                QString & ls = legStrings[i];
                Address & addr = addrsleg[i];
                addr = Address(ls);
            }
            const auto elapsedpleg = Util::getTimeNS() - t0pleg;
            Print() << id << ": Took: " << QString::number(elapsedpleg/1e6, 'f', 6).toUtf8().constData() << " msec";

            Print() << id << ": Parsing " << count << " cashaddr strings ...";
            std::vector<Address> addrsca(count);
            const auto t0pca = Util::getTimeNS();
            for (size_t i = 0; i < count; ++i) {
                QString & cs = caStrings[i];
                Address & addr = addrsca[i];
                addr = Address(cs);
            }
            const auto elapsedpca = Util::getTimeNS() - t0pca;
            Print() << id << ": Took: " << QString::number(elapsedpca/1e6, 'f', 6).toUtf8().constData() << " msec";

            Print() << id << ": Ensuring all equal each other ...";
            for (size_t i = 0; i < count; ++i) {
                if (addrsleg[i] != addrsca[i] || Address::fromPubKey(pubkeys[i], MyKind, MyNet) != addrsca[i])
                    throw Exception(QString("Address index %1 mistmatch").arg(long(i)));
            }
            Print() << id << ": All ok!";
        };

        const size_t N = std::max(std::min(7u, std::thread::hardware_concurrency()), 2u);
        std::vector<std::thread> threads;
        threads.reserve(N);
        for (size_t i = 0; i < N; ++i) {
            threads.emplace_back(Bench, i);
        }
        Log() << N << " threads created.. starting them all now!";
        mut.lock();
        start = true;
        cond.notify_all();
        mut.unlock();
        for (auto & thr : threads) {
            thr.join();
        }
    }

    namespace { const auto b1 = App::registerBench("address", Address::bench); }

    /*static*/
    bool Address::test()
    {
        using Print = Log;
        constexpr auto badAddress = "1C3SoftYBC2bbDbbDzCadZxDrfbnobEXLBLQZbbD";
        constexpr auto anAddress = "qpu3lsv4uufvzsklf38pfl2wckesyuecxgledta0jl";
        constexpr auto anAddress_leg = "1C3SoftYBC2bbDzCadZxDrfbnobEXLBLQZ";
        constexpr auto anAddress2 = "bitcoincash:prnc2exht3zxlrqqcat690tc85cvfuypngh7szx6mk";
        constexpr auto anAddress2_leg = "3NoBpEBHZq6YqwUBdPAMW41w5BTJSC7yuQ";
        constexpr auto anAddress32 = "bitcoincash:pdw6p42faky24gczngpkghpycsmnz92zsmngvhh38mnpyqq6fx6ru2ktumc9l"; // p2sh32
        constexpr auto anAddress32_tok = "bitcoincash:rdw6p42faky24gczngpkghpycsmnz92zsmngvhh38mnpyqq6fx6ruc9hazeu5"; // token-aware p2sh32
        constexpr auto payload32 = "5da0d549ed88aaa3029a03645c24c43731154286e6865ef13ee612001a49b43e";
        constexpr auto reg1 = "mxdxDXaKkdX4vFcMFVZKCcZwTLyykvYXir"; // this is regtest but lacks info on what network it is in verbyte so it will be parsed as testnet because legacy
        constexpr auto reg2 = "bchreg:qzmakj5v6pk8qqf50fv6tc8jc6ew3ldz0vz2n35u44"; // this is regtest and since it's cashaddr will be correctly detected as invalid
        Address a = anAddress, b, bad(badAddress);
        b = a;
        Address c(a);

        {
            int res, cur = 0;
            const int expect[] = { 0, 1, 1, 0};
            Print() << "a < b? " << (res = a < b);
            if (expect[cur++] != res) return false;
            Print() << "a <= b? " << (res = a <= b);
            if (expect[cur++] != res) return false;
            Print() << "a == b? " << (res = a == b);
            if (expect[cur++] != res) return false;
            Print() << "bad is Valid? " << (res = bad.isValid());
            if (expect[cur++] != res) return false;
        }

        c = b;

        Address p2sh(anAddress2);
        Address p2sh_leg(anAddress2_leg);

        constexpr auto testnetStr = "qq9rw090p2eu9drv6ptztwx4ghpftwfa0gyqvlvx2q";
        Address testnet(testnetStr);

        {
            int res, cur = 0;
            const int expect[] = { 1, 0, 0, 1, 0, 1 };
            Print() << "p2sh == p2sh_leg? " << (res = p2sh == p2sh_leg);
            if (expect[cur++] != res) return false;
            Print() << "a == p2sh? " << (res = a == p2sh);
            if (expect[cur++] != res) return false;
            Print() << "a > p2sh? " << (res = a > p2sh);
            if (expect[cur++] != res) return false;
            Print() << "a < p2sh? " << (res = a < p2sh);
            if (expect[cur++] != res) return false;
            Print() << "a >= p2sh? " << (res = a >= p2sh);
            if (expect[cur++] != res) return false;
            Print() << "a <= p2sh? " << (res = a <= p2sh);
            if (expect[cur++] != res) return false;
        }

        Address last;
        struct P {
            const char *str{};
            bool expectedValid{};
            const char *expectedPayload{};
        };
        for (const auto & [str, expectedValid, expectedPayload]
             : {
                P{badAddress, false}, P{anAddress, true}, P{anAddress_leg, true}, P{anAddress2, true},
                P{anAddress32, true, payload32}, P{anAddress32_tok, true, payload32}, P{anAddress2_leg, true},
                P{testnetStr, true}, P{reg1, true}, P{reg2, false},
               }) {
            const Address a{str};
            Print() << "------------------------------------";
            Print() << "Orig string: " << str;
            Print() << "Cash address: " << a.toString().toUtf8().constData() << ", legacy: " << a.toLegacyString().toUtf8().constData() << ", short cashaddr: " << a.toShortString().toUtf8().constData();
            Print() << "Decoded -> VerByte: " << int(a.verByte) <<  "  HashPayload (hex): " << a._hash.toHex().constData();
            Print() << "IsValid: " << a.isValid() << " Kind: " << a.kind() << " Net: " << NetName(a.net()).toUtf8().constData();
            const auto cscript = a.toCScript();
            {
                // ensure 2-way conversion works at least on the blockchain level
                const Address aFromCash(a.toString(false)), aFromLegacy(a.toLegacyString());
                if (aFromCash.toCScript() != cscript || aFromLegacy.toCScript() != cscript)
                    return false;
                // and if we specified an expected payload, ensure both legacy and cash parsing is sane
                if (expectedPayload && (aFromCash.hash() != QByteArray::fromHex(expectedPayload)
                                        || aFromLegacy.hash() != QByteArray::fromHex(expectedPayload)))
                    return false;
            }
            Print() << "Script Hex of: " << a.toString().toUtf8().constData() << " = " << QByteArray(reinterpret_cast<const char *>(&*cscript.begin()), int(cscript.size())).toHex().constData();
            Print() << "HashX of " << a.toString().toUtf8().constData() << " = " << a.toHashX().toHex().constData();
            c = a;
            Print() << "HashX again " << c.toString().toUtf8().constData() << " = " << c.toHashX().toHex().constData();
            Print() << "c==a : " << int(c==a) << "  a==Address()? : " << int(a==Address()) << "  a>Address()?: " << int(a > Address()) << "  a<Address()?: " << int(a < Address());
            Print() << "last==a : " << int(last==a) << "  last>a?: " << int(last > a) << "  last<a?: " << int(last < a) << "  last==a?: " << int(last == a);
            if (a.isValid() != expectedValid)
                return false;
            if (expectedPayload && a.hash() != QByteArray::fromHex(expectedPayload))
                return false;
            last = a;
        }


        Print() << "------------------------------------";
        Print() << "Testing Cash Tokens CashAddr test vectors ...";
        const auto jsondata = R"""([
            {
                "payloadSize": 20,
                "type": 0,
                "cashaddr": "bitcoincash:qr6m7j9njldwwzlg9v7v53unlr4jkmx6eylep8ekg2",
                "payload": "F5BF48B397DAE70BE82B3CCA4793F8EB2B6CDAC9"
            },
            {
                "payloadSize": 20,
                "type": 2,
                "cashaddr": "bitcoincash:zr6m7j9njldwwzlg9v7v53unlr4jkmx6eycnjehshe",
                "payload": "F5BF48B397DAE70BE82B3CCA4793F8EB2B6CDAC9"
            },
            {
                "payloadSize": 20,
                "type": 1,
                "cashaddr": "bchtest:pr6m7j9njldwwzlg9v7v53unlr4jkmx6eyvwc0uz5t",
                "payload": "F5BF48B397DAE70BE82B3CCA4793F8EB2B6CDAC9"
            },
            {
                "payloadSize": 20,
                "type": 0,
                "cashaddr": "bitcoincash:qr7fzmep8g7h7ymfxy74lgc0v950j3r2959lhtxxsl",
                "payload": "FC916F213A3D7F1369313D5FA30F6168F9446A2D"
            },
            {
                "payloadSize": 20,
                "type": 2,
                "cashaddr": "bitcoincash:zr7fzmep8g7h7ymfxy74lgc0v950j3r295z4y4gq0v",
                "payload": "FC916F213A3D7F1369313D5FA30F6168F9446A2D"
            },
            {
                "payloadSize": 20,
                "type": 0,
                "cashaddr": "bchtest:qr7fzmep8g7h7ymfxy74lgc0v950j3r295pdnvy3hr",
                "payload": "FC916F213A3D7F1369313D5FA30F6168F9446A2D"
            },
            {
                "payloadSize": 20,
                "type": 2,
                "cashaddr": "bchtest:zr7fzmep8g7h7ymfxy74lgc0v950j3r295x8qj2hgs",
                "payload": "FC916F213A3D7F1369313D5FA30F6168F9446A2D"
            },
            {
                "payloadSize": 20,
                "type": 0,
                "cashaddr": "bchreg:qr7fzmep8g7h7ymfxy74lgc0v950j3r295m39d8z59",
                "payload": "FC916F213A3D7F1369313D5FA30F6168F9446A2D"
            },
            {
                "payloadSize": 20,
                "type": 2,
                "cashaddr": "bchreg:zr7fzmep8g7h7ymfxy74lgc0v950j3r295umknfytk",
                "payload": "FC916F213A3D7F1369313D5FA30F6168F9446A2D"
            },
            {
                "payloadSize": 20,
                "type": 0,
                "cashaddr": "bitcoincash:qpagr634w55t4wp56ftxx53xukhqgl24yse53qxdge",
                "payload": "7A81EA357528BAB834D256635226E5AE047D5524"
            },
            {
                "payloadSize": 20,
                "type": 2,
                "cashaddr": "bitcoincash:zpagr634w55t4wp56ftxx53xukhqgl24ys77z7gth2",
                "payload": "7A81EA357528BAB834D256635226E5AE047D5524"
            },
            {
                "payloadSize": 20,
                "type": 0,
                "cashaddr": "bitcoincash:qq9l9e2dgkx0hp43qm3c3h252e9euugrfc6vlt3r9e",
                "payload": "0BF2E54D458CFB86B106E388DD54564B9E71034E"
            },
            {
                "payloadSize": 20,
                "type": 2,
                "cashaddr": "bitcoincash:zq9l9e2dgkx0hp43qm3c3h252e9euugrfcaxv4l962",
                "payload": "0BF2E54D458CFB86B106E388DD54564B9E71034E"
            },
            {
                "payloadSize": 20,
                "type": 0,
                "cashaddr": "bitcoincash:qre24q38ghy6k3pegpyvtxahu8q8hqmxmqqn28z85p",
                "payload": "F2AA822745C9AB44394048C59BB7E1C07B8366D8"
            },
            {
                "payloadSize": 20,
                "type": 2,
                "cashaddr": "bitcoincash:zre24q38ghy6k3pegpyvtxahu8q8hqmxmq8eeevptj",
                "payload": "F2AA822745C9AB44394048C59BB7E1C07B8366D8"
            },
            {
                "payloadSize": 20,
                "type": 0,
                "cashaddr": "bitcoincash:qz7xc0vl85nck65ffrsx5wvewjznp9lflgktxc5878",
                "payload": "BC6C3D9F3D278B6A8948E06A399974853097E9FA"
            },
            {
                "payloadSize": 20,
                "type": 2,
                "cashaddr": "bitcoincash:zz7xc0vl85nck65ffrsx5wvewjznp9lflg3p4x6pp5",
                "payload": "BC6C3D9F3D278B6A8948E06A399974853097E9FA"
            },
            {
                "payloadSize": 20,
                "type": 1,
                "cashaddr": "bitcoincash:ppawqn2h74a4t50phuza84kdp3794pq3ccvm92p8sh",
                "payload": "7AE04D57F57B55D1E1BF05D3D6CD0C7C5A8411C6"
            },
            {
                "payloadSize": 20,
                "type": 3,
                "cashaddr": "bitcoincash:rpawqn2h74a4t50phuza84kdp3794pq3cct3k50p0y",
                "payload": "7AE04D57F57B55D1E1BF05D3D6CD0C7C5A8411C6"
            },
            {
                "payloadSize": 20,
                "type": 1,
                "cashaddr": "bitcoincash:pqv53dwyatxse2xh7nnlqhyr6ryjgfdtagkd4vc388",
                "payload": "1948B5C4EACD0CA8D7F4E7F05C83D0C92425ABEA"
            },
            {
                "payloadSize": 20,
                "type": 3,
                "cashaddr": "bitcoincash:rqv53dwyatxse2xh7nnlqhyr6ryjgfdtag38xjkhc5",
                "payload": "1948B5C4EACD0CA8D7F4E7F05C83D0C92425ABEA"
            },
            {
                "payloadSize": 20,
                "type": 1,
                "cashaddr": "bitcoincash:prseh0a4aejjcewhc665wjqhppgwrz2lw5txgn666a",
                "payload": "E19BBFB5EE652C65D7C6B54748170850E1895F75"
            },
            {
                "payloadSize": 20,
                "type": 3,
                "cashaddr": "bitcoincash:rrseh0a4aejjcewhc665wjqhppgwrz2lw5vvmd5u9w",
                "payload": "E19BBFB5EE652C65D7C6B54748170850E1895F75"
            },
            {
                "payloadSize": 20,
                "type": 1,
                "cashaddr": "bitcoincash:pzltaslh7xnrsxeqm7qtvh0v53n3gfk0v5wwf6d7j4",
                "payload": "BEBEC3F7F1A6381B20DF80B65DECA4671426CF65"
            },
            {
                "payloadSize": 20,
                "type": 3,
                "cashaddr": "bitcoincash:rzltaslh7xnrsxeqm7qtvh0v53n3gfk0v5fy6yrcdx",
                "payload": "BEBEC3F7F1A6381B20DF80B65DECA4671426CF65"
            },
            {
                "payloadSize": 32,
                "type": 1,
                "cashaddr": "bitcoincash:pvqqqqqqqqqqqqqqqqqqqqqqzg69v7ysqqqqqqqqqqqqqqqqqqqqqpkp7fqn0",
                "payload": "0000000000000000000000000000123456789000000000000000000000000000"
            },
            {
                "payloadSize": 32,
                "type": 3,
                "cashaddr": "bitcoincash:rvqqqqqqqqqqqqqqqqqqqqqqzg69v7ysqqqqqqqqqqqqqqqqqqqqqn9alsp2y",
                "payload": "0000000000000000000000000000123456789000000000000000000000000000"
            },
            {
                "payloadSize": 32,
                "type": 1,
                "cashaddr": "bitcoincash:pdzyg3zyg3zyg3zyg3zyg3zyg3zyg3zyg3zyg3zyg3zyg3zyg3zyg3jh2p5nn",
                "payload": "4444444444444444444444444444444444444444444444444444444444444444"
            },
            {
                "payloadSize": 32,
                "type": 3,
                "cashaddr": "bitcoincash:rdzyg3zyg3zyg3zyg3zyg3zyg3zyg3zyg3zyg3zyg3zyg3zyg3zygrpttc42c",
                "payload": "4444444444444444444444444444444444444444444444444444444444444444"
            },
            {
                "payloadSize": 32,
                "type": 1,
                "cashaddr": "bitcoincash:pwyg3zyg3zyg3zyg3zyg3zyg3zyg3zyg3zyg3zyg3zyg3zyg3zygsh3sujgcr",
                "payload": "8888888888888888888888888888888888888888888888888888888888888888"
            },
            {
                "payloadSize": 32,
                "type": 3,
                "cashaddr": "bitcoincash:rwyg3zyg3zyg3zyg3zyg3zyg3zyg3zyg3zyg3zyg3zyg3zyg3zygs9zvatfpg",
                "payload": "8888888888888888888888888888888888888888888888888888888888888888"
            },
            {
                "payloadSize": 32,
                "type": 1,
                "cashaddr": "bitcoincash:p0xvenxvenxvenxvenxvenxvenxvenxvenxvenxvenxvenxvenxvcm6gz4t77",
                "payload": "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"
            },
            {
                "payloadSize": 32,
                "type": 3,
                "cashaddr": "bitcoincash:r0xvenxvenxvenxvenxvenxvenxvenxvenxvenxvenxvenxvenxvcff5rv284",
                "payload": "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"
            },
            {
                "payloadSize": 32,
                "type": 1,
                "cashaddr": "bitcoincash:p0llllllllllllllllllllllllllllllllllllllllllllllllll7x3vthu35",
                "payload": "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
            },
            {
                "payloadSize": 32,
                "type": 3,
                "cashaddr": "bitcoincash:r0llllllllllllllllllllllllllllllllllllllllllllllllll75zs2wagl",
                "payload": "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
            }
        ])""";

        const auto vectors = Json::parseUtf8(jsondata, Json::ParseOption::RequireArray).toList();
        for (const auto &var : vectors) {
            const auto m = var.toMap();
            const auto type = m["type"].toUInt();
            const auto payload = QByteArray::fromHex(m["payload"].toString().toUtf8());
            const auto addrString = m["cashaddr"].toString();
            Print() << "Testing \"" << addrString << "\" ...";

            Address addr = Address::fromString(addrString);
            if (!addr.isValid()) {
                Error() << "Failed to parse";
                return false;
            }
            if (type != addr.kind()) {
                Error() << "Wrong type " << type << " != " << addr.kind();
                return false;
            }
            if (payload != addr.hash()) {
                Error() << "Payload is wrong " << payload.toHex() << " != " << addr.hash().toHex();
                return false;
            }
            if (addr.toString(false) != addrString) {
                Error() << "Failed to re-encode as cashaddr correctly";
                return false;
            }
            if (Address a2; (addr.kind() == Address::Kind::P2PKH || addr.kind() == Address::Kind::P2SH)
                             && (!(a2=Address::fromString(addr.toLegacyString())).isValid()
                                 || a2.hash() != addr.hash() || a2.kind() != addr.kind()))  {
                Error() << "Failed to encode as legacy then decode as the same address again: " << a2.toLegacyString();
                return false;
            }
            if (addr.kind() == Address::Kind::P2SH) {
                // test the fromP2SHScriptHash method
                const auto addr2 = Address::fromP2SHScriptHash(addr.hash(), addr.net());
                if (addr2 != addr || addr.toString() != addr2.toString()) {
                    Error() << "addr != addr2: " << addr.toString() << " != " << addr2.toString();
                    return false;
                }
            }
        }
        Print() << vectors.size() << " test vectors passed";

        Print() << "------------------------------------";
        Print() << "Testing Cash Tokens CashAddr \"invalid\" test vectors ...";
        const QString invalidCases[] = {
            "prefix:x32nx6hz",
            "prEfix:x64nx6hz",
            "prefix:x64nx6Hz",
            "pref1x:6m8cxv73",
            "prefix:",
            ":u9wsx07j",
            "bchreg:555555555555555555x55555555555555555555555555udxmlmrz",
            "bchreg:555555555555555555555555555555551555555555555udxmlmrz",
            "pre:fix:x32nx6hz",
            "prefixx64nx6hz",
            "",
            ":",
            "p",
            "p:",
            "p:g",
            "p:gp",
            "p:gpf",
            "p:gpf8",
            "p:gpf8m",
            "p:gpf8m4",
            "p:gpf8m4h",
            "rpzrrzpr:",
            "rqiqkqiqr:",
            "c:qvdy2z3",
        };
        for (const auto &addrString : invalidCases) {
            Print() << "Testing \"" << addrString << "\" ...";
            Address addr = Address::fromString(addrString);
            if (addr.isValid()) {
                Error() << "Expected address to be invalid, was valid instead";
                return false;
            }
            const auto && [prefix, payload] = bitcoin::cashaddr::Decode(addrString.toStdString(), "");
            if (!prefix.empty() || !payload.empty()) {
                Error() << "Expected prefix and payload empty";
                return false;
            }
        }
        Print() << std::size(invalidCases) << " \"invalid\" test vectors passed";

        Print() << "------------------------------------";
        Print() << "address test success";
        return true;
    }

    namespace {
        const auto t1 = App::registerTest("address", []{
            if (!Address::test())
                throw Exception("address test failed");
        });
    }

    namespace {
        // Address <-> scriptPubKey vectors are taken from palladiumcore's src/test/data/key_io_valid.json and
        // src/test/taproot_tests.cpp (P2PKH, P2SH, P2WPKH, P2WSH, P2TR on main / test / regtest).
        const auto t2 = App::registerTest("address_plm", []{
            struct Vec { const char *addr, *scriptHex, *chain; };
            static const Vec vectors[] = {
        {"PHrYiyUQTSekTWvVBNMEnSzV1DCB9TGR2P", "76a91465a16059864a2fdbc7c99a4723a8395bc6f188eb88ac", "main"},
        {"3CMNFxN1oHBc4R1EpboAL5yzHGgE611Xou", "a91474f209f6ea907e2ea48f74fae05782ae8a66525787", "main"},
        {"tEZSNGVUiXapct4zsnZcfz9CfqKhmUqguv", "76a91453c0307d6851aa0ce7825ba883c6bd9ad242b48688ac", "test"},
        {"tEZSNGVUiXapct4zsnZcfz9CfqKhmUqguv", "76a91453c0307d6851aa0ce7825ba883c6bd9ad242b48688ac", "regtest"},
        {"oRvMM9SjaZ8LpnNkN7R8MLj5ZtADdTxKan", "a9146349a418fc4578d10a372b54b45c280cc8c4382f87", "test"},
        {"PJYEqYHSAbeurs8VH1yGEBX9z7WLA3GVdM", "76a9146d23156cbbdcc82a5a47eee4c2c7c583c18b6bf488ac", "main"},
        {"3QjYXhTkvuj8qPaXHTTWb5wjXhdsLAAWVy", "a914fcc5460dd6e2487c7d75b1963625da0e8f4c597587", "main"},
        {"tUyHPhkxH3xAt3EWTAWAN6btewJ4um5SJE", "76a914f1d470f9b02370fdec2e6b708b08ac431bf7a5f788ac", "test"},
        {"oasWYfcYrxaJKoaFcEcT3GWjymSjtrdZqs", "a914c579342c2c4c9220205e2cdc285617040c924a0a87", "test"},
        {"plm1qw508d6qejxtdg4y5r3zarvary0c5xw7kxmmspk", "0014751e76e8199196d454941c45d1b3a323f1433bd6", "main"},
        {"rplm1qw508d6qejxtdg4y5r3zarvary0c5xw7k59erzz", "0014751e76e8199196d454941c45d1b3a323f1433bd6", "regtest"},
        {"tplm1qrp33g0q5c5txsp9arysrx4k6zdkfs4nce4xj0gdcccefvpysxf3q57antz", "00201863143c14c5166804bd19203356da136c985678cd4d27a1b8c6329604903262", "test"},
        {"tplm1qqqqqp399et2xygdj5xreqhjjvcmzhxw4aywxecjdzew6hylgvsescg4a2c", "0020000000c4a5cad46221b2a187905e5266362b99d5e91c6ce24d165dab93e86433", "test"},
        {"rplm1qqqqqp399et2xygdj5xreqhjjvcmzhxw4aywxecjdzew6hylgvsesz3f90s", "0020000000c4a5cad46221b2a187905e5266362b99d5e91c6ce24d165dab93e86433", "regtest"},
        {"plm1p5cyxnuxmeuwuvkwfem96lqzszd02n6xdcjrs20cac6yqjjwudpxq3sa4q9", "5120a60869f0dbcf1dc659c9cecbaf8050135ea9e8cdc487053f1dc6880949dc684c", "main"},
        {"plm1p4qhjn9zdvkux4e44uhx8tc55attvtyu358kutcqkudyccelu0waszqs5k3", "5120a82f29944d65b86ae6b5e5cc75e294ead6c59391a1edc5e016e3498c67fc7bbb", "main"},
        {"plm1p3qkhfews2uk44qtvauqyr2ttdsw7svhkl9nkm9s9c3x4ax5h60wqf2vpwc", "5120882d74e5d0572d5a816cef0041a96b6c1de832f6f9676d9605c44d5e9a97d3dc", "main"},
            };
            const auto scriptHex = [](const Address &a) {
                const auto cs = a.toCScript();
                return QByteArray(reinterpret_cast<const char *>(cs.data()), int(cs.size())).toHex();
            };
            const auto check = [](bool cond, const QString &what) { if (!cond) throw Exception("address_plm test failed: " + what); };
            for (const auto & [addr, hex, chain] : vectors) {
                const Address a = Address::fromString(addr, Coin::PLM);
                check(a.isValid(), QString("valid ") + addr);
                check(a.coin() == Coin::PLM, QString("coin ") + addr);
                check(scriptHex(a) == hex, QString("script ") + addr);
                check(a.toString() == addr && a.toString(true) == addr, QString("round-trip ") + addr);
                // base58 regtest addresses are indistinguishable from testnet (same version bytes), so expect TestNet
                const Net expectNet = QByteArray(chain) == "main" ? MainNet : (QByteArray(addr).contains("plm1") && QByteArray(chain) == "regtest" ? RegTestNet : TestNet);
                check(a.net() == expectNet, QString("net ") + addr);
                check(Address::isValid(addr, expectNet, Coin::PLM), QString("isValid ") + addr);
                if (a.kind() == Address::P2WPKH || a.kind() == Address::P2WSH || a.kind() == Address::P2TR)
                    check(Address::fromString(QString(addr).toUpper(), Coin::PLM) == a, QString("uppercase ") + addr);
                Log() << "address_plm: " << addr << " ok";
            }
            // kinds
            check(Address::fromString("PHrYiyUQTSekTWvVBNMEnSzV1DCB9TGR2P", Coin::PLM).kind() == Address::P2PKH, "kind p2pkh");
            check(Address::fromString("3CMNFxN1oHBc4R1EpboAL5yzHGgE611Xou", Coin::PLM).kind() == Address::P2SH, "kind p2sh");
            check(Address::fromString("plm1qw508d6qejxtdg4y5r3zarvary0c5xw7kxmmspk", Coin::PLM).kind() == Address::P2WPKH, "kind p2wpkh");
            check(Address::fromString("tplm1qrp33g0q5c5txsp9arysrx4k6zdkfs4nce4xj0gdcccefvpysxf3q57antz", Coin::PLM).kind() == Address::P2WSH, "kind p2wsh");
            check(Address::fromString("plm1p3qkhfews2uk44qtvauqyr2ttdsw7svhkl9nkm9s9c3x4ax5h60wqf2vpwc", Coin::PLM).kind() == Address::P2TR, "kind p2tr");
            // a testnet taproot address, built by re-encoding a mainnet program with the "tplm" hrp, must round-trip
            {
                const Address main = Address::fromString("plm1p3qkhfews2uk44qtvauqyr2ttdsw7svhkl9nkm9s9c3x4ax5h60wqf2vpwc", Coin::PLM);
                std::vector<Byte> data{1};
                convertBits<8, 5, true>(data, reinterpret_cast<const Byte *>(main.hash().constData()),
                                        reinterpret_cast<const Byte *>(main.hash().constData()) + main.hash().size());
                for (const auto & [hrp, net] : {std::pair{"tplm", TestNet}, std::pair{"rplm", RegTestNet}}) {
                    const auto str = QString::fromStdString(bech32::Encode(bech32::Encoding::BECH32M, hrp, data));
                    const Address a = Address::fromString(str, Coin::PLM);
                    check(a.isValid() && a.net() == net && a.kind() == Address::P2TR && a.hash() == main.hash() && a.toString() == str,
                          QString("p2tr ") + hrp);
                }
                // witness v0 must be bech32 and v1 must be bech32m (BIP350): swapped encodings are rejected
                std::vector<Byte> v0data{0};
                convertBits<8, 5, true>(v0data, reinterpret_cast<const Byte *>(main.hash().constData()),
                                        reinterpret_cast<const Byte *>(main.hash().constData()) + main.hash().size());
                check(!Address::fromString(QString::fromStdString(bech32::Encode(bech32::Encoding::BECH32M, "plm", v0data)), Coin::PLM).isValid(), "v0 as bech32m rejected");
                check(!Address::fromString(QString::fromStdString(bech32::Encode(bech32::Encoding::BECH32, "plm", data)), Coin::PLM).isValid(), "v1 as bech32 rejected");
                check(Address::fromString(QString::fromStdString(bech32::Encode(bech32::Encoding::BECH32M, "plm", data)), Coin::PLM) == main, "v1 bech32m accepted");
            }
            // invalid: bad checksum, wrong hrp (bitcoin), non-standard witness program, wrong-coin base58
            check(!Address::fromString("plm1qw508d6qejxtdg4y5r3zarvary0c5xw7kxmmspq", Coin::PLM).isValid(), "bad checksum");
            check(!Address::fromString("bc1qw508d6qejxtdg4y5r3zarvary0c5xw7kv8f3t4", Coin::PLM).isValid(), "wrong hrp");
            check(!Address::fromString("plm1zw508d6qejxtdg4y5r3zarvaryvf9mh4q", Coin::PLM).isValid(), "non-standard witness v2");
            check(!Address::fromString("1C3SoftYBC2bbDzCadZxDrfbnobEXLBLQZ", Coin::PLM).isValid(), "BTC/BCH p2pkh is not a PLM address");
            check(!Address::fromString("plm1qw508d6qejxtdg4y5r3zarvary0c5xw7kxmmspk").isValid(), "bech32 needs coin=PLM");
            Log() << "address_plm: all ok";
        });
    }

#endif
} // end namespace BTC
