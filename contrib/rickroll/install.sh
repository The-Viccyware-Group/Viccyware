#!/bin/bash

# Get the directory where the script is located
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"

echo "Installing Rickroll service..."

echo "Copying scripts and audio to /home/root/..."
cp "$SCRIPT_DIR/rickroll.py" /home/root/
cp "$SCRIPT_DIR/rickroll.wav" /home/root/

echo "Copying rickroll.service to /etc/systemd/system/..."
cp "$SCRIPT_DIR/rickroll.service" /etc/systemd/system/

echo "Enabling the service..."
systemctl enable rickroll.service

echo "Done! The service will run on the next boot."
