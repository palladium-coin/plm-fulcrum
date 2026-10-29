#!/usr/bin/env bash
# End-to-end test: plm-fulcrum + isolated regtest palladiumd + PalladiumWallet CLI.
#
# Starts a throw-away regtest node (docker, same image as production, never touches the production
# node), a fresh plm-fulcrum on a temp datadir, then drives the wallet CLI through the flows a real
# Electrum client uses: sync, send/broadcast, mempool, confirmation with Merkle proof, reorg.
#
# Usage:  test/e2e/wallet_e2e.sh            (from the repo root, after building ./plm-fulcrum)
# Env:    FULCRUM=./plm-fulcrum  WALLET_REPO=~/PalladiumWallet  NODE_IMAGE=ghcr.io/palladium-coin/palladiumcore:2.1.0
#         RPC_PORT=19332  TCP_PORT=50911  KEEP=1 (keep node/tmp dir for debugging)
set -euo pipefail

FULCRUM=${FULCRUM:-./plm-fulcrum}
WALLET_REPO=${WALLET_REPO:-$HOME/PalladiumWallet}
NODE_IMAGE=${NODE_IMAGE:-ghcr.io/palladium-coin/palladiumcore:2.1.0}
RPC_PORT=${RPC_PORT:-19332}
TCP_PORT=${TCP_PORT:-50911}
NODE=plm-e2e-node
TMP=$(mktemp -d)
FPID=

cli() { docker exec "$NODE" palladium-cli -regtest -rpcuser=e2e -rpcpassword=e2e -rpcport="$RPC_PORT" "$@"; }
wallet() { dotnet "$WALLET_REPO/src/Cli/bin/Debug/net10.0/PalladiumWallet.Cli.dll" "$@"; }
log() { echo "== $*"; }
fail() { echo "FAIL: $*" >&2; echo "--- fulcrum log tail ---" >&2; tail -n 30 "$TMP/fulcrum.log" >&2 || true; exit 1; }
check() { [ "$2" = "$3" ] || fail "$1: expected '$2', got '$3'"; echo "ok: $1"; }

cleanup() {
    [ -n "$FPID" ] && kill "$FPID" 2>/dev/null && wait "$FPID" 2>/dev/null || true
    if [ -z "${KEEP:-}" ]; then docker rm -f "$NODE" >/dev/null 2>&1 || true; rm -rf "$TMP"; else echo "kept: $NODE, $TMP"; fi
}
trap cleanup EXIT

# Minimal Electrum client: prints the JSON "result" of one call (newline-delimited JSON-RPC over TCP).
electrum() {
    python3 -I - "$TCP_PORT" "$1" "${2:-[]}" <<'PY'
import json, socket, sys
port, method, params = int(sys.argv[1]), sys.argv[2], json.loads(sys.argv[3])
s = socket.create_connection(("127.0.0.1", port), timeout=30)
f = s.makefile("rw")
def call(i, m, p):
    f.write(json.dumps({"id": i, "method": m, "params": p}) + "\n"); f.flush()
    r = json.loads(f.readline())
    if "error" in r and r["error"]: raise SystemExit("electrum error: %s" % r["error"])
    return r["result"]
call(0, "server.version", ["e2e", "1.4"])
print(json.dumps(call(1, method, params)))
PY
}
fulcrum_height() { electrum blockchain.headers.subscribe | python3 -I -c 'import json,sys; print(json.load(sys.stdin)["height"])'; }
wait_height() { # wait until fulcrum reports the node's current height
    local want; want=$(cli getblockcount)
    for _ in $(seq 60); do [ "$(fulcrum_height 2>/dev/null || echo -1)" = "$want" ] && return 0; sleep 1; done
    fail "fulcrum did not reach height $want (at $(fulcrum_height 2>/dev/null || echo ?))"
}
balance() { wallet sync --net regtest --server "127.0.0.1:$TCP_PORT" --file "$1" | sed -n 's/^Balance: *\([0-9.]*\).*/\1/p'; }

log "build wallet CLI"
(cd "$WALLET_REPO" && dotnet build src/Cli -c Debug -v q >/dev/null)

log "start regtest node ($NODE_IMAGE)"
docker rm -f "$NODE" >/dev/null 2>&1 || true
docker run -d --name "$NODE" -p "127.0.0.1:$RPC_PORT:$RPC_PORT" "$NODE_IMAGE" -regtest \
    -server=1 -txindex=1 -listen=0 -fallbackfee=0.0001 \
    -rpcuser=e2e -rpcpassword=e2e -rpcport="$RPC_PORT" -rpcbind=0.0.0.0 -rpcallowip=0.0.0.0/0 >/dev/null
for _ in $(seq 60); do cli getblockcount >/dev/null 2>&1 && break; sleep 1; done
cli getblockcount >/dev/null || fail "node did not start"

log "create wallets"
wallet create --net regtest --file "$TMP/a.json" >"$TMP/a.out"
wallet create --net regtest --file "$TMP/b.json" >"$TMP/b.out"
ADDR_A=$(sed -n 's/^First address: *//p' "$TMP/a.out")
ADDR_B=$(sed -n 's/^First address: *//p' "$TMP/b.out")
MINER=$(cli getnewaddress 2>/dev/null || true)
[ -n "$MINER" ] || { cli createwallet miner >/dev/null; MINER=$(cli getnewaddress); }
echo "A=$ADDR_A B=$ADDR_B"

log "mine 121 blocks to wallet A (coinbase maturity is 120: block 1 becomes spendable)"
cli generatetoaddress 121 "$ADDR_A" >/dev/null
BLOCK1=$(cli getblockhash 1)
SUBSIDY=$(cli getblock "$BLOCK1" 2 | python3 -I -c 'import json,sys; print(sum(o["value"] for o in json.load(sys.stdin)["tx"][0]["vout"]))')

log "start plm-fulcrum on $TCP_PORT"
mkdir -p "$TMP/db"
"$FULCRUM" -D "$TMP/db" -b "127.0.0.1:$RPC_PORT" -u e2e -p e2e -t "127.0.0.1:$TCP_PORT" >"$TMP/fulcrum.log" 2>&1 &
FPID=$!
for _ in $(seq 60); do electrum server.ping >/dev/null 2>&1 && break; kill -0 "$FPID" 2>/dev/null || fail "fulcrum exited"; sleep 1; done
wait_height
check "fulcrum tip hash == node tip hash" "$(cli getbestblockhash)" \
    "$(electrum blockchain.block.header "[$(cli getblockcount)]" | tr -d '"' | python3 -I -c '
import hashlib,sys; h=bytes.fromhex(sys.stdin.read().strip()); print(hashlib.sha256(hashlib.sha256(h).digest()).digest()[::-1].hex())')"
electrum server.banner >/dev/null && echo "ok: server.banner"
electrum blockchain.relayfee >/dev/null && echo "ok: blockchain.relayfee"

log "wallet A sync: matured coinbase of block 1 is spendable"
BAL_A=$(balance "$TMP/a.json")
check "wallet A balance == subsidy of block 1" "$(python3 -I -c "print('%g' % $SUBSIDY)")" "$(python3 -I -c "print('%g' % $BAL_A)")"

log "A sends 1 PLM to B (broadcast through fulcrum)"
wallet send --net regtest --server "127.0.0.1:$TCP_PORT" --file "$TMP/a.json" --to "$ADDR_B" --amount 1 --broadcast >"$TMP/send.out"
TXID=$(sed -n 's/^txid: *//p' "$TMP/send.out")
[ -n "$TXID" ] || fail "no txid from send: $(cat "$TMP/send.out")"
check "tx is in node mempool" "$TXID" "$(cli getrawmempool | python3 -I -c 'import json,sys; m=json.load(sys.stdin); print(m[0] if m else "")')"
# fulcrum picks up new mempool txs on its poll timer (no zmq here): wait until B's history shows it
SH_B=$(cli getaddressinfo "$ADDR_B" | python3 -I -c 'import hashlib,json,sys; print(hashlib.sha256(bytes.fromhex(json.load(sys.stdin)["scriptPubKey"])).digest()[::-1].hex())')
for _ in $(seq 30); do [ "$(electrum blockchain.scripthash.get_history "[\"$SH_B\"]")" != "[]" ] && break; sleep 1; done
[ "$(electrum blockchain.scripthash.get_history "[\"$SH_B\"]")" != "[]" ] || fail "fulcrum never exposed the mempool tx for B"
SYNC_B=$(wallet sync --net regtest --server "127.0.0.1:$TCP_PORT" --file "$TMP/b.json")
grep -q "mempool.*$TXID" <<<"$SYNC_B" && echo "ok: B sees the tx in mempool" || fail "B does not see the mempool tx: $SYNC_B"

log "mine 1 block: tx confirmed, wallet verifies the Merkle proof"
cli generatetoaddress 1 "$MINER" >/dev/null
wait_height
check "B balance == 1" "1" "$(python3 -I -c "print('%g' % $(balance "$TMP/b.json"))")"

log "reorg: invalidate the tip, mine 2 competing blocks"
OLD_TIP=$(cli getbestblockhash)
cli invalidateblock "$OLD_TIP"
cli generatetoaddress 2 "$(cli getnewaddress)" >/dev/null   # different coinbase script, else the block equals the invalidated one
wait_height
NEW_TIP=$(cli getbestblockhash)
[ "$OLD_TIP" != "$NEW_TIP" ] || fail "reorg did not change the tip"
check "fulcrum follows the reorg (height)" "$(cli getblockcount)" "$(fulcrum_height)"
check "B balance still 1 after reorg (tx re-confirmed)" "1" "$(python3 -I -c "print('%g' % $(balance "$TMP/b.json"))")"

log "ALL E2E CHECKS PASSED"
