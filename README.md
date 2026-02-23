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
