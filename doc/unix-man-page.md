% PLM-FULCRUM(1) Version 0.1.0 | plm-fulcrum Manual
% plm-fulcrum was written by Calin Culianu (cculianu); Palladium (PLM) adaptation by Davide Grilli
% October 27, 2026

# NAME

**plm-fulcrum** - A Palladium (PLM) Blockchain SPV Server

# SYNOPSIS

| **plm-fulcrum** \[options\] \[config\]

DESCRIPTION
===========

`plm-fulcrum` requires a `palladiumd` instance running on `mainnet` (or `regtest` for testing), which you must tell it about via the CLI options or via the config file. You also need to tell it what port(s) to listen on and optionally what SSL certificates to use (if using SSL).


**NOTE:**
Electrum-based wallets generally no longer connect to non-SSL servers, so you should probably configure SSL for production use.

It is recommended you specify a data dir (`-D` via CLI or `datadir=` via config file)
on an SSD drive for best results.
Synching against mainnet (about 545,000 blocks) took roughly 2 minutes on a fast
machine with an SSD (measured with the node on the same host); slower machines
and drives will take longer. I have not tried synching on an HDD.

Once the server finishes synching it will behave like an ElectrumX server and it can receive requests from Electrum-protocol clients.

# OPTIONS

-h, --help
:   Prints brief usage information.

-v, --version
:   Displays version information.


-D, --datadir <path>
:   Specify a directory in which to store the database and other assorted data files.  This is a required option. If the specified path does not exist, it will be created. Note that the directory in question should ideally live on a fast drive such as an SSD and it should have plenty of free space available.

-t, --tcp <interface:port>
:   Specify an <interface:port> on which to listen for TCP connections, defaults to 0.0.0.0:50001 (all interfaces, port 50001 -- only if no other interfaces are specified via -t or -s). This option may be specified more than once to bind to multiple interfaces and/or ports.  Suggested values for port: 50001 on mainnet and 60001 on testnet.

-s, --ssl <interface:port>
:   Specify an <interface:port> on which to listen for SSL connections. Note that if this option is specified, then the `cert` and `key` options need to also be specified otherwise the app will refuse to run. This option may be specified more than once to bind to multiple interfaces and/or ports. Suggested values for port: 50002 on mainnet and 60002 on testnet.

-w, --ws <interface:port>
:   Specify an <interface:port> on which to listen for Web Socket connections (unencrypted, ws://). This option may be specified more than once to bind to multiple interfaces and/or ports. Suggested values for port: 50003 on mainnet and 60003 on testnet.

-W, --wss <interface:port>
:   Specify an <interface:port> on which to listen for Web Socket Secure connections (encrypted, wss://). Note that if this option is specified, then the --cert and --key options (or alternatively, the --wss-cert and --wss-key options) need to also be specified otherwise the app will refuse to run. This option may be specified more than once to bind to multiple interfaces and/or ports. Suggested values for port: 50004 on mainnet and 60004 on testnet.

-c, --cert <crtfile>
:   Specify a PEM file to use as the server's SSL certificate.  This option is required if the -s/--ssl and/or the -W/--wss options appear at all on the command-line.  The file should contain either a single valid self-signed certificate or the full certificate chain if using CA-signed certificates.

-k, --key <keyfile>
:   Specify a PEM file to use as the server's SSL key.  This option is required if the -s/--ssl and/or the -W/--wss options apear at all on the command-line.  The file should contain an RSA private key.  EC, DH, and DSA keys are also supported, but their support is experimental.

--wss-cert <crtfile>
:   Specify a certificate PEM file to use specifically for only WSS ports. This option is intended to allow WSS ports to use a CA-signed certificate (required by web browsers), whereas legacy Electrum Cash ports may want to continue to use self-signed certificates. If this option is specified, --wss-key must also be specified. If this option is missing, then WSS ports will just fall-back to using the certificate specified by --cert.

--wss-key <keyfile>
:   Specify a private key PEM file to use for WSS. This key must go with the certificate specified in --wss-cert. If this option is specified, --wss-cert must also be specified.

-a, --admin <[interface:]port>
:   Specify a <port> or an <interface:port> on which to listen for TCP connections for the admin RPC service. The admin service is used for sending special control commands to the server, such as stopping the server, and it should *NOT* be exposed to the internet.  This option is required if you wish to use the plm-fulcrum-admin CLI tool to send commands to plm-fulcrum. It is recommended that you specify the loopback address as the bind interface for this option such as: <port> by itself or 127.0.0.1:<port> for IPv4 and/or ::1:<port> for IPv6. If no interface is specified, and just a port number by itself is used, then IPv4 127.0.0.1 is the bind interface used (along with the specified port). This option may be specified more than once to bind to multiple interfaces and/or ports.

-z, --stats <[interface:]port>
:   Specify listen address and port for the stats HTTP server. Format is same as the -s, -t or -a options, e.g.: <interface:port>. Default is to not start any starts HTTP servers.  Also, like the -a option, you may specify a port number by itself here and 127.0.0.1:<port> will be assumed. This option may be specified more than once to bind to multiple interfaces and/or ports.

-b, --palladiumd <hostname:port>
:   Specify a <hostname:port> to connect to the palladiumd rpc service. This is a required option, along with -u and -p. This hostname:port should be the same as you specified in your palladium.conf file under rpcbind- and rpcport-.

--palladiumd-tls
:   If specified, connect to the remote palladiumd via HTTPS rather than the usual HTTP. palladiumd itself only speaks JSON-RPC over HTTP, so this is only useful behind a TLS-terminating proxy.

-u, --rpcuser <username>
:   Specify a username to use for authenticating to palladiumd. This option should be the same username you specified in your palladium.conf file under rpcuser-. For security, you may omit this option from the command-line and use the RPCUSER environment variable instead (the CLI arg takes precedence if both are present), or you may use -K instead.

-p, --rpcpassword <password>
:   Specify a password to use for authenticating to palladiumd. This option should be the same password you specified in your palladium.conf file under rpcpassword-. For security, you may omit this option from the command-line and use the RPCPASSWORD environment variable instead (the CLI arg takes precedence if both are present), or you may use -K instead.

-K, --rpccookie <cookiefile>
:   This option can be used instead of -u and -p. The file path for the palladiumd '.cookie' file (normally lives inside palladiumd's datadir). This file is auto-generated by palladiumd and (re)connect to palladiumd. Use this option only if your palladiumd is using cookie-file based RPC authentication.

-d, --debug
:   Print extra verbose debug output. This is the default on debug builds. This is the opposite of -q. (Specify this options twice to get network-level trace debug output.)

-q, --quiet
:   Suppress debug output. This is the default on release builds. This is the opposite of -d.

-S, --syslog
:   Syslog mode. If on Unix, use the syslog() facility to produce log messages. This option currently has no effect on Windows.

-C, --checkdb
:   If specified, database consistency will be checked thoroughly for sanity & integrity. Note that these checks are somewhat slow to perform and under normal operation are not necessary. May be specified twice to do even more thorough checks.

-T, --polltime <polltime>
:   The number of seconds for the palladiumd poll interval. Palladiumd is polled once every `polltime` seconds to detect mempool and blockchain changes. This value must be at least 0.5 and cannot exceed 30. If not specified, defaults to 2 seconds.

--ts-format <keyword>
:   Specify log timestamp format, one of: "none", "uptime", "localtime", or "utc". If unspecified, default is "localtime" (previous versions of Fulcrum always logged using "uptime").

--tls-disallow-deprecated
:   If specified, restricts the TLS protocol used by the server to non-deprecated v1.2 or newer, disallowing connections from clients requesting TLS v1.1 or earlier. This option applies to all SSL and WSS ports server-wide.

--no-simdjson
:   If specified, disable the fast simdjson backend for JSON parsing. This parser is over 2x faster than the original parser, and is enabled by default as of Fulcrum version 1.3.0.

--bd-timeout
:   Corresponds to the configuration file variable "palladiumd_timeout". The number of seconds to wait for unanswered palladiumd requests before we consider them as having timed-out (default: 30). You may wish to set this higher than the default if you see "palladiumd request timed out" appear in the log.

--bd-clients
:   Corresponds to the configuration file variable "palladiumd_clients". The number of simultaneous RPC clients that we spawn to connect to palladiumd (default: 3). If you raise this value from the default, be sure to also specify the option `rpcthreads=` to palladiumd so that there are enough threads to accommodate the clients we spawn, otherwise you may get errors from palladiumd.

--compact-dbs
:   If specified, plm-fulcrum will compact all databases on startup. The compaction process reduces database disk space usage by removing redundant/unused data. Note that rocksdb normally compacts the databases in the background while plm-fulcrum is running, so using this option to explicitly compact the database files on startup is not strictly necessary.

--pidfile
:   If specified, plm-fulcrum will write its process ID to this file on startup. Useful for integration with monitoring software.

--upnp
:   Use UPnP to tell your router to automatically allow incoming connections for all all TCP, SSL, WS, and WSS ports. To explicitly disable UPnP, use the CLI arg --no-upnp. Default is: disabled.

--db_mem <MB>
:   Specify roughly the maximum amount of memory to give to rocksb. Larger values offer better performance, at the expense of memory consumption. Specify a floating-point or integer value in MiB (1 MiB = 1048576 bytes). Default is: 1024.0.

--db-upgrade
:   Enable Fulcrum 1.x -> 2.x DB upgrade. Use this option the first time you install Fulcrum 2.x if you wish to upgrade the already-synched 1.x datadir to the new 2.x format.

--dump-sh <outputfile>
:   *This is an advanced debugging option*. Dump script hashes. If specified, after the database is loaded, all of the script hashes in the database will be written to outputfile as a JSON array.

[config]
:   Configuration file (optional). To read configuration variables from the environment instead, specify `_ENV_` for this argument.


# FILES

*/etc/plm-fulcrum.conf*

:   Default config file when running as a service.

# PORTS

The default port is 50001 and 50002 for TCP and SSL, respectively.

# BUGS

See GitHub Issues: the issue tracker of this repository
