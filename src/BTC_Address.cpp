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
#include "bitcoin/script.h"

#include <QHash>
#include <QHashFunctions>

#include <mutex>
#include <span>
#include <utility>

#ifdef ENABLE_TESTS
#  include "App.h"
#  include "Compat.h"
#  include <QRandomGenerator>
#  include <iostream>
#  include <thread>
#  include <utility>
#endif

namespace BTC
{
    inline Compat::qhuint qHash(BTC::Net n, Compat::qhuint seed = 0) noexcept { return ::qHash(uchar(n), seed); }

    namespace {
        // PLM (Palladium) base58 version bytes; same values as palladiumcore's chainparams.cpp. Regtest shares testnet's.
        // Hash of Net -> [Hash of VerByte -> Kind]
        const QHash<Net, QHash<quint8, Address::Kind> > netVerByteKindMap = {
            { MainNet,      { {55, Address::P2PKH }, {5, Address::P2SH} } },
            { TestNet,    { {127, Address::P2PKH }, {115, Address::P2SH} } },
            { RegTestNet, { {127, Address::P2PKH }, {115, Address::P2SH} } },
        };
        /// PLM bech32 human-readable part for the net, or nullptr if the net has no PLM equivalent.
        const char *hrpForNet(Net net) {
            switch (net) {
            case MainNet: return "plm";
            case TestNet: return "tplm";
            case RegTestNet: return "rplm";
            default: return nullptr;
            }
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
        Byte verByteForNetAndKind(Net net, Address::Kind kind) {
            if (Address::isWitnessKind(kind))
                // for witness kinds "verByte" holds the witness version
                return hrpForNet(net) ? (kind == Address::P2TR ? 1 : 0) : Address::InvalidVerByte;
            if (const auto map = netVerByteKindMap.value(net); !map.isEmpty()) [[likely]] {
                for (auto it = map.begin(); it != map.end(); ++it) {
                    if (it.value() == kind)
                        return it.key();
                }
            }
            return Address::InvalidVerByte;
        }
        Address::Kind kindForNetAndVerByte(Net net, Byte verByte) {
            if (const auto map = netVerByteKindMap.value(net); !map.isEmpty()) [[likely]] {
                for (auto it = map.begin(); it != map.end(); ++it) {
                    if (it.key() == verByte)
                        return it.value();
                }
            }
            return Address::Kind::Invalid;
        }
        /// NB: This won't ever auto-detect regtest since it has the same verBytes as testnet (127 & 115).  Since
        /// between the two choices, testnet is the much more likely network anybody using this software will be using,
        /// we always return testnet if the verByte matches regtest and/or testnet.
        Net netForVerByte(Byte verByte) {
            for (auto it = netVerByteKindMap.begin(); it != netVerByteKindMap.end(); ++it) {
                const Net net = it.key();
                if (net == RegTestNet)
                    // don't auto-detect regtest net -- skip it.
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
        other.verByte = verByteForNetAndKind(other._net, other._kind);
        // true if both encodings match
        return other.isValid() && toString() == other.toString();
    }

    /*static*/
    Address Address::fromString(const QString &str)
    {
        static const auto DecodeWitness = [] (Address & a, const std::string &ss) -> bool {
            std::string hrp;
            std::vector<Byte> data;
            const auto enc = bech32::Decode(ss, hrp, data);
            if (enc == bech32::Encoding::INVALID || data.empty() || data[0] > 16)
                return false;
            Net net = Net::Invalid;
            for (const Net n : {MainNet, TestNet, RegTestNet}) {
                if (hrp == hrpForNet(n)) { net = n; break; }
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
            a._net = net;
            a._kind = kind;
            a.verByte = witver;
            a._hash = QByteArray(reinterpret_cast<const char *>(prog.data()), int(prog.size()));
            return true;
        };
        Address a;
        std::vector<Byte> dec;
        if (const auto ss = str.toStdString(); bitcoin::DecodeBase58Check(ss, dec)) {
            // base58 address (P2PKH or P2SH)
            if (const auto decsize = dec.size(); decsize != 1 + H160Len) {
#ifdef QT_DEBUG
                DebugM(__func__, ": bad decoded length  ", dec.size(), " for address ", str);
#endif
            } else {
                a.verByte = dec[0];
                a._hash.resize(decsize - 1);
                std::memcpy(a._hash.data(), &dec[1], a._hash.size());
                a._net = netForVerByte(a.verByte); // note this will not correctly differntiate between TestNet and RegTestNet and always pick TestNet since they have the same verBytes.
                a.autosetKind(); // this will clear the address if something is wrong
            }
        } else {
            // bech32 / bech32m address
            bool ok = false;
            try {
                if (!(ok = DecodeWitness(a, ss))) {
#ifdef QT_DEBUG
                    DebugM(__func__, ": got bad address ", str);
#endif
                }
            } catch (const std::exception &e) {
                Warning() << "Internal error decoding address " << str << ": " << e.what();
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
        if (k == Kind::P2PKH) [[likely]] {
            if (const auto vb = verByteForNetAndKind(net, k); vb != InvalidVerByte && pend > pbegin) [[likely]] {
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
        ret.verByte = verByteForNetAndKind(ret._net, ret._kind);
        return ret;
    }


    // Used internally in one branch when parsing base58.  If it fails it will clear the address to default constructed values.
    bool Address::autosetKind()
    {
        if (_hash.length() == int(H160Len)) {
            _kind = kindForNetAndVerByte(_net, verByte);
            if (_kind != Kind::Invalid)
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
            if (_kind == P2PKH) {
                ret << OP_DUP << OP_HASH160 << hbytes << OP_EQUALVERIFY << OP_CHECKSIG;
            } else if (_kind == P2SH) {
                ret << OP_HASH160 << hbytes << OP_EQUAL;
            }
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

    /// if isValid:  Returns the address string (base58 for P2PKH and P2SH, bech32/bech32m for the witness kinds)
    /// if !isValid: Returns the empty string.
    QString Address::toString() const {
        QString ret;
        if (isValid()) {
            if (isWitnessKind(_kind)) {
                if (const char *hrp = hrpForNet(_net); hrp) {
                    std::vector<Byte> data{verByte};
                    convertBits<8, 5, true>(data, reinterpret_cast<const Byte *>(_hash.constData()),
                                            reinterpret_cast<const Byte *>(_hash.constData()) + _hash.size());
                    ret = QString::fromStdString(bech32::Encode(_kind == P2TR ? bech32::Encoding::BECH32M
                                                                              : bech32::Encoding::BECH32, hrp, data));
                }
            } else {
                std::vector<Byte> vch;
                vch.reserve(1 + size_t(_hash.size()));
                vch.push_back(verByte);
                vch.insert(vch.end(), _hash.begin(), _hash.end());
                ret = QString::fromStdString(bitcoin::EncodeBase58Check(vch));
            }
        }
        return ret;
    }

    /*static*/
    bool Address::isValid(const QString &str, Net net)
    {
        const Address a = Address::fromString(str);
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

            Print() << id << ": Generating " << count << " address strings ...";
            std::vector<QString> strings(count);
            const auto t0ls = Util::getTimeNS();
            for (size_t i = 0; i < count; ++i) {
                const auto a = Address::fromPubKey(pubkeys[i], MyKind, MyNet);
                strings[i] = a.toString();
            }
            const auto elapsedls = Util::getTimeNS() - t0ls;
            Print() << id << ": Took: " << QString::number(elapsedls/1e6, 'f', 6).toUtf8().constData() << " msec";
            Print() << id << ": Last string: " << strings.back().toUtf8().constData();

            Print() << id << ": Parsing " << count << " address strings ...";
            std::vector<Address> addrs(count);
            const auto t0p = Util::getTimeNS();
            for (size_t i = 0; i < count; ++i) {
                addrs[i] = Address(strings[i]);
            }
            const auto elapsedp = Util::getTimeNS() - t0p;
            Print() << id << ": Took: " << QString::number(elapsedp/1e6, 'f', 6).toUtf8().constData() << " msec";

            Print() << id << ": Ensuring all equal the originals ...";
            for (size_t i = 0; i < count; ++i) {
                if (Address::fromPubKey(pubkeys[i], MyKind, MyNet) != addrs[i])
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

    namespace {
        // Address <-> scriptPubKey vectors are taken from palladiumcore's src/test/data/key_io_valid.json and
        // src/test/taproot_tests.cpp (P2PKH, P2SH, P2WPKH, P2WSH, P2TR on main / test / regtest).
        const auto t1 = App::registerTest("address", []{
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
            const auto check = [](bool cond, const QString &what) { if (!cond) throw Exception("address test failed: " + what); };
            for (const auto & [addr, hex, chain] : vectors) {
                const Address a = Address::fromString(addr);
                check(a.isValid(), QString("valid ") + addr);
                check(scriptHex(a) == hex, QString("script ") + addr);
                check(a.toString() == addr, QString("round-trip ") + addr);
                // base58 regtest addresses are indistinguishable from testnet (same version bytes), so expect TestNet
                const Net expectNet = QByteArray(chain) == "main" ? MainNet : (QByteArray(addr).contains("plm1") && QByteArray(chain) == "regtest" ? RegTestNet : TestNet);
                check(a.net() == expectNet, QString("net ") + addr);
                check(Address::isValid(addr, expectNet), QString("isValid ") + addr);
                if (a.kind() == Address::P2WPKH || a.kind() == Address::P2WSH || a.kind() == Address::P2TR)
                    check(Address::fromString(QString(addr).toUpper()) == a, QString("uppercase ") + addr);
                Log() << "address: " << addr << " ok";
            }
            // kinds
            check(Address::fromString("PHrYiyUQTSekTWvVBNMEnSzV1DCB9TGR2P").kind() == Address::P2PKH, "kind p2pkh");
            check(Address::fromString("3CMNFxN1oHBc4R1EpboAL5yzHGgE611Xou").kind() == Address::P2SH, "kind p2sh");
            check(Address::fromString("plm1qw508d6qejxtdg4y5r3zarvary0c5xw7kxmmspk").kind() == Address::P2WPKH, "kind p2wpkh");
            check(Address::fromString("tplm1qrp33g0q5c5txsp9arysrx4k6zdkfs4nce4xj0gdcccefvpysxf3q57antz").kind() == Address::P2WSH, "kind p2wsh");
            check(Address::fromString("plm1p3qkhfews2uk44qtvauqyr2ttdsw7svhkl9nkm9s9c3x4ax5h60wqf2vpwc").kind() == Address::P2TR, "kind p2tr");
            // a testnet taproot address, built by re-encoding a mainnet program with the "tplm" hrp, must round-trip
            {
                const Address main = Address::fromString("plm1p3qkhfews2uk44qtvauqyr2ttdsw7svhkl9nkm9s9c3x4ax5h60wqf2vpwc");
                std::vector<Byte> data{1};
                convertBits<8, 5, true>(data, reinterpret_cast<const Byte *>(main.hash().constData()),
                                        reinterpret_cast<const Byte *>(main.hash().constData()) + main.hash().size());
                for (const auto & [hrp, net] : {std::pair{"tplm", TestNet}, std::pair{"rplm", RegTestNet}}) {
                    const auto str = QString::fromStdString(bech32::Encode(bech32::Encoding::BECH32M, hrp, data));
                    const Address a = Address::fromString(str);
                    check(a.isValid() && a.net() == net && a.kind() == Address::P2TR && a.hash() == main.hash() && a.toString() == str,
                          QString("p2tr ") + hrp);
                }
                // witness v0 must be bech32 and v1 must be bech32m (BIP350): swapped encodings are rejected
                std::vector<Byte> v0data{0};
                convertBits<8, 5, true>(v0data, reinterpret_cast<const Byte *>(main.hash().constData()),
                                        reinterpret_cast<const Byte *>(main.hash().constData()) + main.hash().size());
                check(!Address::fromString(QString::fromStdString(bech32::Encode(bech32::Encoding::BECH32M, "plm", v0data))).isValid(), "v0 as bech32m rejected");
                check(!Address::fromString(QString::fromStdString(bech32::Encode(bech32::Encoding::BECH32, "plm", data))).isValid(), "v1 as bech32 rejected");
                check(Address::fromString(QString::fromStdString(bech32::Encode(bech32::Encoding::BECH32M, "plm", data))) == main, "v1 bech32m accepted");
            }
            // invalid: bad checksum, wrong hrp (bitcoin), non-standard witness program, wrong-coin base58
            check(!Address::fromString("plm1qw508d6qejxtdg4y5r3zarvary0c5xw7kxmmspq").isValid(), "bad checksum");
            check(!Address::fromString("bc1qw508d6qejxtdg4y5r3zarvary0c5xw7kv8f3t4").isValid(), "wrong hrp");
            check(!Address::fromString("plm1zw508d6qejxtdg4y5r3zarvaryvf9mh4q").isValid(), "non-standard witness v2");
            check(!Address::fromString("1C3SoftYBC2bbDzCadZxDrfbnobEXLBLQZ").isValid(), "BTC p2pkh is not a PLM address");
            Log() << "address: all ok";
        });
    }

#endif
} // end namespace BTC
