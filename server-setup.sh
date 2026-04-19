#!/bin/bash
# Server setup script for temperature-data-collector
# Run as a user with sudo privileges on a Debian/Ubuntu host.

set -e

RED="\033[0;31m"
BLUE="\033[1;34m"
GREEN="\033[0;32m"
YELLOW="\033[1;33m"
BOLD="\033[1m"
NC="\033[0m"

header() { echo -e "\n${BOLD}${BLUE}=== $* ===${NC}"; }
ok()     { echo -e "${GREEN}✓  $*${NC}"; }
warn()   { echo -e "${YELLOW}⚠  $*${NC}"; }
err()    { echo -e "${RED}✗  $*${NC}"; exit 1; }

# ---------------------------------------------------------------------------
# [1/4] Docker
# ---------------------------------------------------------------------------
header "[1/4] Docker"

if command -v docker &>/dev/null && docker compose version &>/dev/null; then
  ok "Docker and Compose plugin already installed, skipping."
else
  read -rp "Docker (or Compose plugin) not found. Install now? [Y/n]: " INSTALL_DOCKER
  if [[ ! "$INSTALL_DOCKER" =~ ^[Nn]$ ]]; then
    sudo apt update
    sudo apt install -y apt-transport-https ca-certificates curl gnupg lsb-release
    curl -fsSL "https://download.docker.com/linux/$(. /etc/os-release; echo "$ID")/gpg" \
      | sudo gpg --dearmor -o /usr/share/keyrings/docker-archive-keyring.gpg
    echo \
      "deb [arch=$(dpkg --print-architecture) \
      signed-by=/usr/share/keyrings/docker-archive-keyring.gpg] \
      https://download.docker.com/linux/$(. /etc/os-release; echo "$ID") \
      $(lsb_release -cs) stable" \
      | sudo tee /etc/apt/sources.list.d/docker.list > /dev/null
    sudo apt update
    sudo apt install -y docker-ce docker-ce-cli containerd.io docker-compose-plugin
    ok "Docker installed."
  else
    warn "Skipping Docker install. Ensure Docker and the Compose plugin are available before continuing."
  fi
fi

# ---------------------------------------------------------------------------
# [2/4] Environment file
# ---------------------------------------------------------------------------
header "[2/4] Environment"

if [ -f .env ]; then
  warn ".env already exists — updating credentials in place."
else
  cp env_sample .env
  ok "Created .env from env_sample."
fi

echo "MQTT credentials (used by collectors and the subscriber):"
read -rsp "  Publisher password: "  MQTT_PUB_PASS; echo ""
read -rsp "  Subscriber password: " MQTT_SUB_PASS; echo ""

[[ -z "$MQTT_PUB_PASS" ]] && err "MQTT publisher password cannot be empty."
[[ -z "$MQTT_SUB_PASS" ]] && err "MQTT subscriber password cannot be empty."

sed -i "s|MQTT_PUB_PASS=.*|MQTT_PUB_PASS=${MQTT_PUB_PASS}|" .env
sed -i "s|MQTT_SUB_PASS=.*|MQTT_SUB_PASS=${MQTT_SUB_PASS}|" .env
ok "MQTT credentials written."

# ---------------------------------------------------------------------------
# [3/4] MySQL
# ---------------------------------------------------------------------------
header "[3/4] MySQL"
echo "Choose a mode:"
echo "  1) Containerized (default) — Docker manages MySQL, no external server needed"
echo "  2) External                — Connect to an existing MySQL server"
echo "  3) Disabled                — Redis-only (data is lost on container restart)"
echo ""
read -rp "Choice [1]: " MYSQL_MODE
MYSQL_MODE="${MYSQL_MODE:-1}"

case "$MYSQL_MODE" in
  1)
    echo ""
    echo "Containerized MySQL credentials (blank = use default in [brackets]):"
    read -rp  "  Database name [sensordata]: " DB_NAME; DB_NAME="${DB_NAME:-sensordata}"
    read -rp  "  Database user [weather]: "    DB_USER; DB_USER="${DB_USER:-weather}"
    read -rsp "  Database password: "          DB_PASS; echo ""
    read -rsp "  MySQL root password: "        DB_ROOT; echo ""

    [[ -z "$DB_PASS" ]] && err "Database password cannot be empty."
    [[ -z "$DB_ROOT" ]] && err "MySQL root password cannot be empty."

    sed -i "s|USE_MYSQL=.*|USE_MYSQL=True|"                               .env
    sed -i "s|DB_HOST=.*|DB_HOST=localhost|"                              .env
    sed -i "s|DB_NAME=.*|DB_NAME=${DB_NAME}|"                             .env
    sed -i "s|DB_USER=.*|DB_USER=${DB_USER}|"                             .env
    sed -i "s|DB_PASS=.*|DB_PASS=${DB_PASS}|"                             .env
    sed -i "s|MYSQLDB_ROOT_PASSWORD=.*|MYSQLDB_ROOT_PASSWORD=${DB_ROOT}|" .env

    ok "Containerized MySQL configured. Schema initializes automatically on first boot."
    ;;

  2)
    echo ""
    echo "External MySQL connection:"
    read -rp  "  Host: "                       DB_HOST
    read -rp  "  Database name [sensordata]: " DB_NAME; DB_NAME="${DB_NAME:-sensordata}"
    read -rp  "  Username: "                   DB_USER
    read -rsp "  Password: "                   DB_PASS; echo ""

    [[ -z "$DB_HOST" ]] && err "Host cannot be empty."
    [[ -z "$DB_USER" ]] && err "Username cannot be empty."
    [[ -z "$DB_PASS" ]] && err "Password cannot be empty."

    sed -i "s|USE_MYSQL=.*|USE_MYSQL=True|"   .env
    sed -i "s|DB_HOST=.*|DB_HOST=${DB_HOST}|" .env
    sed -i "s|DB_NAME=.*|DB_NAME=${DB_NAME}|" .env
    sed -i "s|DB_USER=.*|DB_USER=${DB_USER}|" .env
    sed -i "s|DB_PASS=.*|DB_PASS=${DB_PASS}|" .env

    echo ""
    read -rp "Initialize the schema on the external server now? [Y/n]: " INIT_SCHEMA
    if [[ ! "$INIT_SCHEMA" =~ ^[Nn]$ ]]; then
      mysql -h "$DB_HOST" -u "$DB_USER" -p"$DB_PASS" \
        -e "CREATE DATABASE IF NOT EXISTS \`${DB_NAME}\`;"
      mysql -h "$DB_HOST" -u "$DB_USER" -p"$DB_PASS" "$DB_NAME" < sensordump.sql
      ok "Schema created on ${DB_HOST}."
    fi

    warn "Comment out the 'mysql' service in docker-compose.yml and remove"
    warn "the 'mysql' entry from mqtt-subscriber's depends_on."
    ok "External MySQL configured."
    ;;

  3)
    sed -i "s|USE_MYSQL=.*|USE_MYSQL=False|" .env
    warn "MySQL disabled. Data held in Redis only — lost on container restart."
    warn "Comment out the 'mysql' service in docker-compose.yml and remove"
    warn "the 'mysql' entry from mqtt-subscriber's depends_on."
    ;;

  *)
    err "Invalid choice '${MYSQL_MODE}'. Re-run the script to configure MySQL."
    ;;
esac

# ---------------------------------------------------------------------------
# [4/4] Launch
# ---------------------------------------------------------------------------
header "[4/4] Launch"

echo "Starting services..."
docker compose up --build -d

SERVER_IP=$(hostname -I | awk '{print $1}')
echo ""
ok "Stack is up."
echo -e "    Dashboard : ${BOLD}http://${SERVER_IP}:8080${NC}"
echo -e "    Logs      : docker compose logs -f"
echo ""
echo -e "${YELLOW}Firewall reminder: open TCP 1883 (MQTT) and 8080 (dashboard) if applicable.${NC}"
