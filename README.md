# socket_prac

This repository contains a TLS chat server/client with PostgreSQL support.

## AWS Ubuntu Deployment (Binary Upload Flow)

### 0) Prerequisites

- Local machine:
  - `ssh`, `scp`, `cmake`, compiler toolchain
  - this repository cloned
- AWS EC2 (Ubuntu):
  - public IP reachable
  - security group allows:
    - SSH `22` from your IP
    - app port `8080` (or your configured server port)
- PEM key file for the EC2 instance

### 1) Build local binaries

```bash
cd /path/to/socket_prac
./scripts/build_server.sh
```

Expected output binaries:

- `./server`
- `./client`

### 2) Deploy to EC2 with the deploy script

```bash
cd /path/to/socket_prac
./scripts/deploy_aws_ubuntu.sh --host <EC2_PUBLIC_IP> --key ~/instance.pem
```

What this uploads:

- `/home/ubuntu/socket_prac/server`
- `/home/ubuntu/socket_prac/client`
- `/home/ubuntu/socket_prac/.env`
- `/home/ubuntu/socket_prac/config/`
- `/home/ubuntu/socket_prac/bootstrap_ubuntu.sh`
- `/home/ubuntu/socket_prac/scripts/`

Default TLS automation in deploy script:

- regenerates remote certs with host-matching SAN (`--auto-tls 1`)
- downloads remote `certs/ca.crt.pem` to local `./ca.crt.pem` (`--pull-ca 1`)

## Run bootstrap on server (required for first-time setup)

After deployment, run bootstrap on EC2.

```bash
ssh -i ~/instance.pem ubuntu@<EC2_PUBLIC_IP>
cd /home/ubuntu/socket_prac
chmod +x bootstrap_ubuntu.sh
```

Choose one:

1. Runtime dependencies only (binary already uploaded):

```bash
INSTALL_POSTGRES=0 PROVISION_DB=0 MIGRATE_DB_SCHEMA=0 GENERATE_TLS_CERTS=0 BUILD_SERVER=0 ./bootstrap_ubuntu.sh
```

2. Local PostgreSQL + DB provisioning/migration:

```bash
INSTALL_POSTGRES=1 PROVISION_DB=1 MIGRATE_DB_SCHEMA=1 GENERATE_TLS_CERTS=0 BUILD_SERVER=0 ./bootstrap_ubuntu.sh
```

`GENERATE_TLS_CERTS=0` is recommended here because deploy step already regenerated certs for the EC2 host.

## Start server on EC2

```bash
cd /home/ubuntu/socket_prac
./server
```

## Connect client from local machine

Use the CA file downloaded by deploy script:

```bash
cd /path/to/socket_prac
./client <EC2_PUBLIC_IP> 8080 ./ca.crt.pem
```

If you changed server port in `config/server.conf`, use that port in the client command.

## Client Usage

Client executable usage:

```bash
./client [ip] [port] [ca_path]
```

- default `ip`: `127.0.0.1`
- default `port`: `8080`
- default `ca_path`: `certs/ca.crt.pem`

Example (local server):

```bash
./client 127.0.0.1 8080 certs/ca.crt.pem
```

Basic flow after connect:

1. Create account: `/register <id> <pw>`
2. Login: `/login <id> <pw>`
3. Create room: `/create_room <room_name>`
4. List rooms: `/list_room`
5. Select room: `/select_room <room_id>`
6. Send message: `<text>`
7. Load history: `/history <room_id> <limit>`

Main chat commands:

- `/register <id> <pw>`
- `/login <id> <pw>`
- `/nick <nickname>`
- `/friend_request <user_id>`
- `/friend_accept <user_id>`
- `/friend_reject <user_id>`
- `/friend_remove <user_id>`
- `/list_friend`
- `/list_friend_request`
- `/create_room <room_name>`
- `/delete_room <room_id>`
- `/invite_room <room_id> <friend_user_id>`
- `/leave_room <room_id>`
- `/select_room <room_id>`
- `/list_room`
- `/history <room_id> <limit>` (`limit`: 1~100)
- `/help`

## Useful deploy options

```bash
./scripts/deploy_aws_ubuntu.sh --help
```

Common options:

- `--remote-dir /home/ubuntu/socket_prac`
- `--auto-tls 0|1`
- `--tls-san-dns <value>`
- `--tls-san-ip <value>`
- `--pull-ca 0|1`
- `--local-ca-out <path>`
