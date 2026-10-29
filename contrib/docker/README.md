# Running plm-fulcrum with Docker Compose

This runs plm-fulcrum in a container, on the same Docker network as the Palladium node, so it reaches the node by its
container name. Data is kept in a directory on the host (`~/.plm-fulcrum`), the same way the node keeps its data in
`~/.palladium`.

## Requirements

- Docker with the Compose plugin (`docker compose`), usable without `sudo`.
- A Palladium node started with its own compose file (`palladiumcore/docker/docker-compose.yml`). That file creates the
  Docker network `palladium` that plm-fulcrum joins, so **start the node first**.
- The node must run with `txindex=1`, must not be pruned, and must accept RPC connections from Docker networks (the node's
  compose file already sets `rpcbind=0.0.0.0` and `rpcallowip=172.16.0.0/12`).
- The RPC user and password of the node (`rpcuser` / `rpcpassword` in its `palladium.conf`).

## Steps

All commands are run from the root of this repository.

1. **Check that the node is up and the network exists:**

   ```
   docker ps --filter name=docker-palladiumcore
   docker network ls --filter name=palladium
   ```

2. **Create the data directory and the configuration file:**

   ```
   mkdir -p ~/.plm-fulcrum
   cp contrib/docker/plm-fulcrum.conf.example ~/.plm-fulcrum/plm-fulcrum.conf
   chmod 600 ~/.plm-fulcrum/plm-fulcrum.conf
   ```

   Edit `~/.plm-fulcrum/plm-fulcrum.conf` and set `rpcuser` and `rpcpassword` to the node's values. `palladiumd` is
   already `docker-palladiumcore:2332` (the node's container name and RPC port). Don't set `datadir`, `cert` or `key`:
   the container sets them. The file holds the RPC password, so keep it out of version control.

3. **Build the image and start the container:**

   ```
   docker compose -f contrib/docker/docker-compose.yml up -d --build
   ```

   The first build compiles the server and takes about ten minutes. Later builds reuse the cached layers.

4. **Follow the log until the server is synchronized:**

   ```
   docker compose -f contrib/docker/docker-compose.yml logs -f
   ```

   The first start downloads and indexes the whole chain from the node (under two minutes on the machine used to test
   this, with the node on the same host). When the log shows `up-to-date`, the server is ready. Clients can connect to port `50001` (tcp) or `50002` (ssl).

## Day to day

| Task | Command |
|---|---|
| Stop | `docker compose -f contrib/docker/docker-compose.yml stop` |
| Start again | `docker compose -f contrib/docker/docker-compose.yml start` |
| Log | `docker compose -f contrib/docker/docker-compose.yml logs -f` |
| Update after a `git pull` | `docker compose -f contrib/docker/docker-compose.yml up -d --build` |
| Remove the container | `docker compose -f contrib/docker/docker-compose.yml down` (the data in `~/.plm-fulcrum` is kept) |

The container restarts automatically after a reboot or a crash (`restart: unless-stopped`), and it is given up to two
minutes to stop, so the database is flushed cleanly.

## Settings

- **Data directory:** `~/.plm-fulcrum` by default. Set `PLM_FULCRUM_DATADIR` to use another path.
- **User:** the container runs as user `1000:1000`, so the files in the data directory belong to that user. If your
  user has another id, set `PLM_UID` and `PLM_GID` (see `id -u` / `id -g`).
- **Ports:** `50001` and `50002` are published on all interfaces of the host. To listen on the host's localhost only,
  change the mappings in `docker-compose.yml` to `"127.0.0.1:50001:50001"` and `"127.0.0.1:50002:50002"`.
- **Other options:** any option of [doc/plm-fulcrum-example-config.conf](../../doc/plm-fulcrum-example-config.conf) can be
  added to `~/.plm-fulcrum/plm-fulcrum.conf`, except `datadir`, `cert` and `key`. Restart the container to apply them.

## Opening the server to other machines

Opening ports `50001` and `50002` on the host's firewall is enough for the server to be reachable. Keep in mind that:

- Docker publishes the ports itself and bypasses the rules of host firewalls such as `ufw`. To restrict who can connect,
  bind to localhost as above and put a reverse proxy or a network rule in front.
- Port `50001` is not encrypted.
- Port `50002` uses a self-signed certificate, created on the first start as `plm-fulcrum.crt` and `plm-fulcrum.key` in
  the data directory, and clients have to accept it explicitly. For a public server, replace both files with a real
  certificate (same names) and restart the container.
- A request whose reply is larger than `max_buffer` (8 MB by default) gets a `response too large` error. This happens
  for addresses with a very large history. See the `max_buffer` option in the example config.

## Troubleshooting

- **`network palladium declared as external, but could not be found`:** the node is not started. Start it first.
- **`Permission denied` on `/data`:** the data directory is not owned by the user the container runs as. Fix the owner
  (`sudo chown -R 1000:1000 ~/.plm-fulcrum`) or set `PLM_UID` / `PLM_GID`.
- **The log shows RPC errors or `401`:** `rpcuser` / `rpcpassword` in `plm-fulcrum.conf` do not match the node's.
- **The log can't reach the node:** check that both containers are on the `palladium` network
  (`docker network inspect palladium`) and that the node's `rpcallowip` includes the Docker networks.
- **The node is refused:** plm-fulcrum only works with a Palladium node (`/Palladium:` subversion), with `txindex=1`.

## Publishing an image

`contrib/docker/build.sh <image tag>` builds a multi-architecture image (`linux/arm64` and `linux/amd64`) with `buildx` and
pushes it to the registry in the tag, so you must be logged in to that registry first. It is not needed to run the
server with the compose file above, which builds the image locally.
