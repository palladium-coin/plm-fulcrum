# plm-fulcrum

A fast & nimble Electrum-protocol SPV server for Palladium (PLM).

#### Palladium (PLM) adaptation
This repository is a fork of [Fulcrum](https://github.com/cculianu/Fulcrum) by Calin Culianu, adapted to support
Palladium (PLM). It is **not** the official Fulcrum.

Forked from upstream `master` at commit [`02e22da`](https://github.com/cculianu/Fulcrum/commit/02e22da92355893d8696305f0741acb7a8bc61f4)
("Refreshed servers*.json for BCH and BTC", upstream version 2.1.4); plm-fulcrum starts at version 0.1.0. All PLM-specific changes live on the `main` branch.

#### Copyright
(C) 2019-2026 Calin Culianu <calin.culianu@gmail.com>

(C) 2026 Davide Grilli (Palladium (PLM) adaptation)

#### License:
GPLv3. See the included `LICENSE.txt` file or [visit gnu.org and read the license](https://www.gnu.org/licenses/gpl-3.0.html).

### Highlights:

- *Fast:* Written in 100% modern `C++20` using multi-threaded and asynchronous programming techniques.
- *A drop-in replacement for ElectrumX:* it speaks the Electrum 1.6 protocol over TCP/SSL/WS/WSS. Installation and management are nearly identical to an ElectrumX server.
- *Cross-platform:* Developed and tested on Linux, macOS and Windows; it should work on any modern OS that has Qt5 or Qt6 Networking available.
- *Palladium only:* PLM (SegWit and Taproot, no CashTokens, no MWEB) is the only supported coin. A database created for any other coin is refused.

### Requirements

- *For running*:
  - A Palladium full node (`palladiumd` 2.1.0 or later) with its JSON-RPC service enabled, preferably running on the same machine. No other full nodes are supported.
    - The node must have txindex enabled e.g. `txindex=1`.
    - The node must not be a pruning node.
    - *Optional*: for best results, enable zmq for the "hashblock" topic using e.g. `zmqpubhashblock=tcp://0.0.0.0:28332` in your `palladium.conf` file.
  - *Recommended hardware*: Minimum 1GB RAM, 64-bit CPU, and enough disk space for the PLM index (a few GB at the current chain height). For best results, use an SSD rather than an HDD.
- *For compiling*:
  - `Qt Core` & `Qt Networking` libraries `5.15.2` or above.  Qt `5.15.1` (or earlier) is not supported.
  - *Optional but recommended*:
    - `libzmq 4.x` development headers and library (also known as `libzmq3-dev` on Debian/Ubuntu and `zeromq-devel` on Fedora). The server will run just fine without linking against `libzmq`, but it will run better if you do link against it and also turn on `zmqpubhashblock` notifications in `palladiumd`.
    - `libminiupnpc 2.x/3.x` development headers and library (also known as `libminiupnpc-dev` on Debian/Ubuntu and `miniupnpc-devel` on Fedora). Needed only if you want UPnP to open up firewall ports on your router (CLI arg: `--upnp`, conf var: `upnp=true`).
  - A modern, 64-bit `C++20` compiler.  `clang-17` or `g++-13` are recommended. MSVC on Windows is not supported (please use `MinGW G++` instead, which ships with Qt Open Source Edition for Windows).

### Quickstart

1. Build the server from source (see [How To Compile](#how-to-compile)) or with the static Docker build (see below).
2. Verify that the binary runs on your system by executing the binary with `./plm-fulcrum -h` to see the CLI options.
3. Setup a configuration file and to point plm-fulcrum to your `palladiumd` JSON-RPC server, specify listening ports, TLS certificates, etc.  See: [doc/plm-fulcrum-example-config.conf](doc/plm-fulcrum-example-config.conf) and/or [doc/plm-fulcrum-quick-config.conf](doc/plm-fulcrum-quick-config.conf)
4. Also see this section below on [Running plm-fulcrum](#running-plm-fulcrum).

### How To Compile

No pre-built binaries are published for this fork yet, so you need to compile it (or use the static Docker build below). To compile, it's recommended you use the Qt Creator IDE.

1. Get the latest version of Qt Open Source Edition for your platform.
2. Point the Qt Creator IDE at the `plm-fulcrum.pro` file.
3. Set the build configuration to "Release".  Hit Build.  It should "just work".

You may also build from the CLI (on Linux and MacOS):

1. Make sure you have `qmake` in your path and all the requisite Qt5 dev libs installed.
2. `qmake` (to generate the Makefile)
3. `make -j8`  (replace 8 here with the number of cores on your machine)

**A note for Linux users**: You may have to install the Qt5 or Qt6 networking package separately such as `libqt5network5` or `libqt6network6` (depending on your distribution). You also need `libbz2-dev` otherwise compilation will fail. If you are having trouble finding the required Qt versions, you can try this link: https://launchpad.net/~beineri (for Ubuntu/Debian ppas). For best results, you may wish to also ensure you have the following installed: `pkg-config`, `libzmq` (aka `libzmq3-dev` on Debian/Ubuntu, `zeromq-devel` on Fedora), and `libminiupnpc` (aka `libminiupnpc-dev` on Debian/Ubuntu, `miniupnpc-devel` on Fedora).

**A note for Windows users**: `Qt 5.15.2` (or above) with `MinGW G++ 11.x.x` (or above) is the compiler/Qt kit you should be using.  MSVC is not supported by this codebase at the present time.

#### What to do if compiling fails
If you have problems compiling, the most likely culprit would be your compiler not being `C++20` compliant (please use a recent version of `gcc` or `clang` on Linux, Apple's `Xcode` on Mac, or `MinGW G++ 11.x` or above on Windows).

The other likely culprit is the fact that at the present time I have included a statically-built `librocksdb` in the codebase. There are versions of this library for Windows, Mac, and Linux included right in the source tree, and `plm-fulcrum.pro` looks for them and links to them. Instructions are included within the `plm-fulcrum.pro` project file about how to build your own static `librocksdb` if the bundled one does not work on your system.

If you are still having trouble, file an issue in this repository.

#### Linking against the system `librocksdb.so` (experimental)

You may optionally build against the **system rocksdb** (Linux only) if your distribution offers `rocksdb` version `6.6.4` or newer.

1. `qmake LIBS+=-lrocksdb`  (to generate the Makefile **without** linking to the included static lib)
2. `make clean && make -j8` (replace 8 here with the number of cores on your machine)

**Note**: Some Linux distributions have been known to package `librocksdb.so` incorrectly. [See here for an example](https://bugs.archlinux.org/task/65093), so until I can be confident most distributions do it right, I am considering using the system `librocksdb.so` an ***experimental feature*** for the time being (in principle it should work ok if the library is compiled correctly).

#### Making sure `libzmq` is detected and used (optional but recommended)

Ensure that `libzmq3` (Debian/Ubuntu) and/or `zeromq-devel` (Fedora/Redhat) is installed, and that `pkg-config` is also installed.  If on Unix (macOS, Linux, or Windows MinGW), then ideally the `qmake` step will find `libzmq` on your system and automatically use it. If that is not the case, you may try passing flags to `qmake` such as `LIBS+="-L/path/to/libdir_containting_libzmq -lzmq"` and `INCLUDEPATH+="/path/to/dir_containing_zmq_h"` as arguments when you invoke `qmake`.  Using `libzmq` is optional but highly recommended.

#### Making sure `libminiupnpc` is detected and used (optional)

Ensure that `libminiupnpc` (Debian/Ubuntu) and/or `miniupnpc-devel` (Fedora/Redhat) is installed, and that `pkg-config` is also installed.  If on Unix (macOS, Linux, or Windows MinGW), then ideally the `qmake` step will find `libminiupnpc` on your system and automatically use it. If that is not the case, you may try passing flags to `qmake` such as `LIBS+="-L/path/to/dir_containing_libminiupnpc -lminiupnpc"` and `INCLUDEPATH+="/path/to/dir_containing_miniupnpc_headers_dir"` as arguments when you invoke `qmake`.  Using `libminiupnpc` is optional but necessary if you want UPnP support.

### Building the Windows static `plm-fulcrum.exe`

There is a mechanism using Docker to build a statically-linked
Windows `.exe`. This build is 100% compatible with any stock 64-bit Windows 7 or
above system -- you don't have to install anything -- it *just works*. 

If you want to build it yourself though, you can do so, but it requires
[Docker](https://www.docker.com/) on either a MacOS or a Linux host system (it
may work on Windows too with Linux tools for Windows -- but I haven't tried it
myself). It builds *all* dependencies, including a static Qt and static rocksdb.
As such, it may take a while so be patient.

1. Make sure Docker is installed such that you don't need to use `sudo`. This is the default on MacOS, but on Linux you may need to [follow these instructions here](https://docs.docker.com/install/linux/linux-postinstall/).

2. Run the build script:

    `$ contrib/build/build.sh windows main`

The first argument to the script is the platform to build (in this case
`windows`). The second argument to the script is a git `branch` or `tag` to
build. Two `.exe` files will be generated, `plm-fulcrum.exe` and `plm-fulcrum-admin.exe`,
which will appear in `dist/win` after the build process completes.

- *Note:* You can point the build script to any repository, not just this one, by giving it a `GIT_REPO` environment variable:

    `$ GIT_REPO=https://github.com/myusername/my-plm-fulcrum-fork contrib/build/build.sh windows main`

    `$ GIT_REPO=$(pwd) contrib/build/build.sh windows main`

### Building a static executable for Linux

There is a mechanism using Docker to build a statically-linked
Linux executable. This build is 100% compatible with most stock 64-bit Linux
systems with a new enough glibc. So on a relatively modern Linux system, you
don't have to install anything -- it *just works*.

If you want to build it yourself though, you can do so, but it requires [Docker](https://www.docker.com/)
on either a MacOS or a Linux host system.  It builds a static Qt and static rocksdb.

1. Make sure Docker is installed such that you don't need to use `sudo`. This is the default on MacOS, but on Linux you may need to [follow these instructions here](https://docs.docker.com/install/linux/linux-postinstall/).

2. Run the build script:

    `$ contrib/build/build.sh linux main`

The first argument to the script is the platform to build (in this case
`linux`). The second argument to the script is a git `branch` or `tag` to build.

- *Note:* You can point the build script to any repository, not just this one, by giving it a `GIT_REPO` environment variable:

    `$ GIT_REPO=https://github.com/myusername/my-plm-fulcrum-fork contrib/build/build.sh linux main`

    `$ GIT_REPO=$(pwd) contrib/build/build.sh linux main`

---

### Running plm-fulcrum

Execute the binary, with `-h` to see the built-in help, e.g. `./plm-fulcrum -h`. You can set most options from the CLI, but you can also specify a **config file** as an argument. See:

 - [doc/plm-fulcrum-example-config.conf](doc/plm-fulcrum-example-config.conf) in the source tree. This sample config file is very well documented with comments.
 - [doc/plm-fulcrum-quick-config.conf](doc/plm-fulcrum-quick-config.conf) in the source tree. This is a more abbreviated config file you can use as a starting point as well.

`plm-fulcrum` requires a `palladiumd` instance running on `mainnet` (or `regtest` for testing), which you must tell it about via the CLI options or via the config file.  You also need to tell it what port(s) to listen on and optionally what SSL certificates to use (if using SSL). ***Note:*** *Electrum-based wallets generally no longer connect to non-SSL servers, so you should probably configure SSL for production use*.

It is recommended you specify a data dir (`-D` via CLI or `datadir=` via config file) on an SSD drive for best results.  Synching against mainnet (about 545,000 blocks) took roughly 2 minutes on a fast machine with an SSD (measured with the node on the same host); slower machines and drives will take longer. I have not tried synching on an HDD.

As long as the server is still synchronizing, all public-facing ports will not yet be bound for listening and as such an attempt to connect to one of the RPC ports will fail with a socket error such as e.g. "Connection refused". Once the server finishes synching it will behave like an ElectrumX server and it can receive requests from Electrum-protocol clients.

You may also wish to read the [plm-fulcrum manpage](doc/unix-man-page.md).


#### Admin Script: plm-fulcrum-admin

`plm-fulcrum` comes with an admin script (`Python 3.6+` is required on the system to run this script).  You may send commands to `plm-fulcrum` using this script. The script requires that an **admin port** (config var `admin=`, CLI arg `-a`) be configured for your server.  To run the script, execute `./plm-fulcrum-admin -h` and you will see a list of possible subcommands that you can send to `plm-fulcrum`. Below you see all available commands (the below assumes the `admin` port is on port `8000`):

    $ ./plm-fulcrum-admin -p 8000 addpeer                 Add a peer to the server's list of peers
    $ ./plm-fulcrum-admin -p 8000 ban                     Ban clients by ID and/or IP address
    $ ./plm-fulcrum-admin -p 8000 banpeer                 Ban peers by hostname suffix
    $ ./plm-fulcrum-admin -p 8000 palladiumd_throttle     Query or set server palladiumd_throttle setting
    $ ./plm-fulcrum-admin -p 8000 clients (sessions)      Print information on all the currently connected clients
    $ ./plm-fulcrum-admin -p 8000 getinfo                 Get server information
    $ ./plm-fulcrum-admin -p 8000 kick                    Kick clients by ID and/or IP address
    $ ./plm-fulcrum-admin -p 8000 listbanned (banlist)    Print the list of banned IP addresses and peer hostnames
    $ ./plm-fulcrum-admin -p 8000 loglevel                Set the server's logging verbosity
    $ ./plm-fulcrum-admin -p 8000 maxbuffer               Query or set server max_buffer setting
    $ ./plm-fulcrum-admin -p 8000 peers                   Print peering information
    $ ./plm-fulcrum-admin -p 8000 query                   Query for balance, UTXO, and history information for one or more addresses
    $ ./plm-fulcrum-admin -p 8000 rmpeer                  Remove peers by hostname suffix
    $ ./plm-fulcrum-admin -p 8000 simdjson                Get or set the server's 'simdjson' (JSON parser) setting
    $ ./plm-fulcrum-admin -p 8000 stop (shutdown)         Gracefully shut down the server
    $ ./plm-fulcrum-admin -p 8000 unban                   Unban IP addresses
    $ ./plm-fulcrum-admin -p 8000 unbanpeer               Unban peers by hostname suffix

---

### Protocol Documentation

The server implements the Electrum protocol 1.6 (see the [Electrum protocol documentation](https://electrumx.readthedocs.io/en/latest/protocol.html)).

---

### Platform Notes

#### Windows

This codebase will not compile correctly (or at all) using MSVC. Please use the `MinGW` and/or `G++` kit in Qt Creator to build this software.

#### Linux

Be sure you have a recent gcc that support C++20. I am using gcc-13 in my docker builders, but gcc-11 *should* work too.

#### MacOS

Everything should just work (I use MacOS as my dev machine).

---

### F.A.Q.



**Q:** Why Qt?  This isn't a GUI app!

**A:** Yes, I know.  However, Qt is a very robust, cross-platform and fast application framework.  You can use its "Core" library for console apps, servers, etc.  It has great network support and other basic things a programmer needs to get stuff done.
