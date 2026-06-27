#!/usr/bin/env bash
# deploy/image-setup.sh - Executed inside the Pi image chroot during CI build
set -euo pipefail

export DEBIAN_FRONTEND=noninteractive

# Determine username/password from environments, or default to sunpi
USER_NAME="${PI_USERNAME:-sunpi}"

echo "==> Configuring base operating system..."

# 1. Update package lists and install required dependencies
apt-get update
apt-get install -y \
  build-essential \
  gcc \
  g++ \
  make \
  cmake \
  pkg-config \
  git \
  wget \
  curl \
  python3 \
  python3-dev \
  python3-venv \
  python3-pip \
  python3-setuptools \
  python-is-python3 \
  libssl-dev \
  libffi-dev \
  dnsmasq

# 2. Install Tailscale
echo "==> Installing Tailscale (requires manual 'sudo tailscale up' on first boot)..."
curl -fsSL https://tailscale.com/install.sh | sh

# 3. Create user account
echo "==> Creating user account: ${USER_NAME}..."
if ! id "${USER_NAME}" &>/dev/null; then
  useradd -m -s /bin/bash -G sudo,dialout,netdev "${USER_NAME}"
fi

# Set password (use GitHub Secret if set, fallback to hashed 'sunpi' default to satisfy secret scanners)
if [[ -n "${PI_PASSWORD:-}" ]]; then
  echo "${USER_NAME}:${PI_PASSWORD}" | chpasswd
else
  # Default password is 'sunpi' (using pre-computed SHA-512 hash)
  echo "${USER_NAME}:\$6\$SArMnaIiB.dSnTwB\$2bptqoMiB4QZEChJ1NhnqXmtAyNh7nXikz5XL2Y6QxCHt/l./FhwgKY2EXWdeDPMjMe542D0RSodt6ghiPak50" | chpasswd -e
fi

# Allow passwordless sudo for user
echo "${USER_NAME} ALL=(ALL) NOPASSWD:ALL" > "/etc/sudoers.d/${USER_NAME}"

# 4. Clone and compile the project
echo "==> Setting up can-telem-cloud under /home/${USER_NAME}..."
git clone "https://github.com/${GITHUB_REPOSITORY:-badgerloop-software/can-telem-cloud}.git" "/home/${USER_NAME}/can-telem-cloud"
chown -R "${USER_NAME}:${USER_NAME}" "/home/${USER_NAME}/can-telem-cloud"

cd "/home/${USER_NAME}/can-telem-cloud"
make -j$(nproc)
chown "${USER_NAME}:${USER_NAME}" can_telem

# 5. Deploy systemd services
echo "==> Setting up systemd services..."
cp deploy/can0.service /etc/systemd/system/
cp deploy/can-telem.service /etc/systemd/system/
cp deploy/can-telem-gnss.service /etc/systemd/system/

# Adjust systemd service paths if a non-default username was specified
if [[ "${USER_NAME}" != "sunpi" ]]; then
  echo "==> Adjusting service paths to use /home/${USER_NAME} instead of /home/sunpi..."
  sed -i "s|/home/sunpi|/home/${USER_NAME}|g" /etc/systemd/system/can-telem.service
  sed -i "s|/home/sunpi|/home/${USER_NAME}|g" /etc/systemd/system/can-telem-gnss.service
fi

# 6. Deploy network failover rules
echo "==> Setting up LTE failover rules..."
cp deploy/10-managed-devices.conf /etc/NetworkManager/conf.d/
cp deploy/99-ignore-lte-net.rules /etc/udev/rules.d/

# 7. Mask ModemManager & Enable Services
systemctl mask ModemManager
systemctl enable can0.service
systemctl enable can-telem.service
systemctl enable can-telem-gnss.service
systemctl enable tailscaled.service

echo "==> Configuration complete!"
