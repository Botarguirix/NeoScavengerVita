#!/usr/bin/env bash
# Instala el cliente de vita-agent-use dentro de Ubuntu (WSL). Se ejecuta como tu usuario.
set -e
SRC=/mnt/d/Proyectos/NeoScavengerVita/tools/vita/vita-agent-use
if [ ! -d ~/vita-agent-use ]; then
  mkdir -p ~/vita-agent-use
  cp -r "$SRC/client" "$SRC/docs" "$SRC/README.md" "$SRC/LICENSE" ~/vita-agent-use/
fi
python3 -m venv ~/va-venv
~/va-venv/bin/pip install -q cryptography
cat > ~/wsl-agent.sh <<'EOF'
#!/usr/bin/env bash
export VITA_AGENT_CONFIG_DIR="$HOME/vita-agent-config"
cd ~/vita-agent-use
exec ~/va-venv/bin/python client/vita_agent.py "$@"
EOF
chmod +x ~/wsl-agent.sh
~/va-venv/bin/python -c "import cryptography; print('cryptography', cryptography.__version__)"
~/wsl-agent.sh --help | head -5
