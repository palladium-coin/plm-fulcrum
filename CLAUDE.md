# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

This repo is named **plm-fulcrum** (remote `origin`, branches `main` and `dev`). It is a fork of [Fulcrum](https://github.com/cculianu/Fulcrum) (C++20 / Qt Electrum-protocol SPV server, forked from upstream commit `02e22da`), adapted to **Palladium (PLM)**. The PLM work lives on the `main` branch; `master` tracks upstream. Keep GPLv3 and upstream copyright notices intact (see `LICENSE.txt`).

Fulcrum indexes a full node's chain into RocksDB and serves the Electrum protocol 1.6 over TCP/SSL/WS/WSS. PLM is the **only** supported coin: BCH, BTC, LTC/MWEB, CashTokens, DSProof, RPA and the extra nets have been removed. PLM is a Bitcoin Core fork (SegWit, Taproot, no CashTokens). Version is `0.1.0` (`VERSION` in `src/Common.h`, bumped with `contrib/bump-version.sh <ver>`); `APPNAME` is `plm-fulcrum`; the binaries are `plm-fulcrum` and `plm-fulcrum-admin` (a Python script). Options and config keys are named `palladiumd*` (`--palladiumd`/`-b`, `palladiumd_tls`, ...); internal classes such as the `BTC` namespace keeps its upstream name; the node RPC classes are `PalladiumDMgr`/`PalladiumD` (`src/PalladiumD.*`).

## Build and test

Build system is qmake (`plm-fulcrum.pro`), not CMake. Requires Qt >= 5.15.2 (Core + Network), a C++20 compiler (clang-17 / g++-13), and `libbz2-dev`. A static RocksDB is bundled in `staticlibs/`; optional `libzmq` and `libminiupnpc` are autodetected via pkg-config.

```
qmake && make -j8                    # release build, produces ./plm-fulcrum and ./plm-fulcrum-admin
qmake LIBS+=-lrocksdb                # experimental: link system rocksdb instead of the bundled one
contrib/build/build.sh linux main    # static docker build (see README)
```

Tests/benchmarks are compiled in only when `DEFINES += ENABLE_TESTS` is uncommented in `plm-fulcrum.pro` (it is off by default). Then:

```
./plm-fulcrum --test all                 # run all tests; `--test <name>` runs one (`-h` lists available names)
./plm-fulcrum --bench <name>             # `--bench all` runs every benchmark
```

There is no linter. Tests are registered at namespace scope with `App::registerTest(...)` / `registerBench(...)` (`src/App.h`), spread across `src/Json/tests.cpp`, `src/bitcoin/test.cpp` and `src/tests/`. Standalone programs in `test/` are not part of the build.

CI (`.github/workflows/test.yml`, on push to `main`/`dev` and PRs) runs: tests with g++-13 and clang++-17 (`qmake DEFINES+=ENABLE_TESTS`, `--test all`), release build + smoke test, lcov coverage (needs `-fprofile-update=atomic`, tests are multithreaded), and a build of `contrib/docker/Dockerfile`. It does not publish images or releases. Code must compile with both g++-13 and clang++-17 (libstdc++ 13/14): check a file quickly with `clang++-18 -std=c++20 -fsyntax-only -fPIC -Isrc -I/usr/include/x86_64-linux-gnu/qt5 -I/usr/include/x86_64-linux-gnu/qt5/QtCore <file>`.

Run against a node: see `doc/plm-fulcrum-example-config.conf`. The node must have `txindex=1` and not be pruned; `zmqpubhashblock` is recommended.

## Architecture

The main flow spans several files:

- **`App`** parses CLI/config (`Options`), creates the singleton subsystems and owns the process lifecycle.
- **`PalladiumDMgr`** (`PalladiumD.cpp`) manages JSON-RPC connections to the full node. On connect it parses `getnetworkinfo`'s `subversion` to identify the node and refuses any node that is not Palladium (`/Palladium:`); it emits `coinDetected(BTC::Coin)`. RPC behavior (estimatesmartfee, zmq, no submitpackage) is derived here.
- **`Controller`** is the sync engine. It downloads blocks and mempool (`Controller/SynchMempoolTask`), deserializes them with `BTC::Deserialize<CBlock>` (always SegWit), checks reorgs, and writes through `Storage`. Detected coin vs. DB-stored coin mismatch is rejected here.
- **`Storage`** (`Storage.cpp`, ~5.8k lines) is the RocksDB schema: headers, tx-num to hash mapping, scripthash history/UTXO tables, undo info, and `Meta` (stores `coin` and `chain` as strings, so the coin name string is persisted in the DB and must never change once used).
- **`SrvMgr`/`Servers`** implement the Electrum protocol (`Server::rpc_*` handlers), subscriptions (`SubsMgr`, `SubStatus`) and the `Mempool` view.
- **`PeerMgr`** does Electrum server peering. It loads `:resources/<lowercase-coin>/servers*.json` from the Qt resource file (`resources.qrc`), so each coin needs its own directory there.
- **`BTC.h` / `BTC_Address`** hold the `Coin` and `Net` enums and address handling. The `src/bitcoin/` tree is vendored Bitcoin Core / BCHN code (tx, block, base58, bech32; script verification was removed) and is modified in places (SegWit deserialization, marked "MODIFIED BY CALIN").

## PLM adaptation status

`Coin::PLM` (persisted coin name `"PLM"`, never change it) is the only coin (`Coin` is `{Unknown, PLM}`); SegWit is always on. Touch points, useful when grepping `Coin::PLM`:

1. `BTC.h` / `BTC.cpp`: `Coin` enum and `coinName` / `coinFromName`.
2. `PalladiumD.cpp`: the node is identified from `subversion` starting with `/Palladium:`; any other node is refused.
3. `BTC_Address.cpp`: per-coin base58 and bech32/bech32m (P2WPKH, P2WSH, P2TR) params, with tests. PLM mainnet: P2PKH 55, P2SH 5, hrp `plm`; testnet: 127 / 115 / `tplm`; regtest hrp `rplm`.
4. `resources/plm/servers.json` and `servers_testnet.json` (listed in `resources.qrc`) for PeerMgr. Peering is off by default and an empty seed list is tolerated.
5. `Servers.cpp`: the donation address is empty unless `donation=` is configured.
6. Default RPC port in example configs is 2332 (testnet 12332).

The PLM full node (`palladiumd`, source in `~/palladiumcore`) does not support `submitpackage` or `getindexinfo`. Genesis hash is read from the node at runtime (`getblockhash 0`), never hardcoded. Remaining work (naming cleanup, docs, final verification) is tracked in `TODO.md`.

## Environment quirks

- No `libbz2-dev` here and `sudo` needs a password: link with `LIBRARY_PATH=<dir>` holding a `libbz2.so` symlink to `/usr/lib/x86_64-linux-gnu/libbz2.so.1.0`.
- Build tests without editing the `.pro`: `qmake DEFINES+=ENABLE_TESTS`.
- `make distclean` does not work after the renames: delete `*.o`, `moc_*`, `qrc_*` and `Makefile` by hand (never `git clean -X`, it would hit `staticlibs/`).
- `--bench all` aborts on the first bench needing external input (`HEXJSON`, `DATADIR`, `TFILE`, `MPDAT`, `BITCOIND`); run benches one by one.
- Static build without pushing: `GIT_REPO=$PWD script -qfec "contrib/build/build.sh linux main" log` (needs a TTY; its last step fails on root-owned files, the binary stays in `contrib/build/linux/work/built/`, remove `work/` with a root docker run).
- Docker node `docker-palladiumcore` (net `palladium`, `172.18.0.2:2332`, user `username`) is the production mainnet node: PLM has no testnet, use mainnet or regtest. For regtest, stop it, edit `~/.palladium/palladium.conf` (network-specific options like `port`, `rpcport`, `rpcbind` go in `[main]` and `[regtest]`) and restore it afterwards. If one ZMQ address fails to bind, Core disables all of them.
- Qt queued signals need their argument types registered in `src/register_MetaTypes.cpp` (e.g. `size_t`).
- Don't put `std::pair<X, T>` (or similar) inside `T`'s own members while `T` is incomplete: clang + libstdc++ 13/14 recurse in the default-constructibility check (see `Entry` in `src/Json/Json_Parser.cpp`).
- CI logs: `gh run list -R palladium-coin/plm-fulcrum`, then `gh api repos/palladium-coin/plm-fulcrum/actions/jobs/<job-id>/logs | grep -i error` (`gh run view --log-failed` only works once the whole run has finished). Claude has no GitHub credentials for `git push` (HTTPS); the user pushes.
- The persisted coin string `"PLM"` and the `nBitcoinDRequests` getinfo key are kept on purpose; the node RPC classes are `PalladiumD*`.

## Testing against a real client

PalladiumWallet CLI (`~/PalladiumWallet`, `dotnet run --project src/Cli -- sync --server 127.0.0.1:50001 --net mainnet|regtest --file <path>`) works as an end-to-end check; always pass `--file` so the user's real wallet is not touched. Known bug: replies larger than `max_buffer` (8 MB, e.g. an address with ~97k txs) disconnect the client (see `bug.md` on `dev`).
