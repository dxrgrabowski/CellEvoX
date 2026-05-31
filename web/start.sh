#!/usr/bin/env bash
# CellEvoX Web — Start Script
# Usage: ./web/start.sh [--backend-only | --frontend-only]

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
BACKEND_DIR="$SCRIPT_DIR/backend"
FRONTEND_DIR="$SCRIPT_DIR/frontend"

# ── Colours ──────────────────────────────────────────────────────────
CYAN='\033[0;36m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${CYAN}"
echo "  ██████╗███████╗██╗     ██╗     ███████╗██╗   ██╗ ██████╗ ██╗  ██╗"
echo " ██╔════╝██╔════╝██║     ██║     ██╔════╝██║   ██║██╔═══██╗╚██╗██╔╝"
echo " ██║     █████╗  ██║     ██║     █████╗  ██║   ██║██║   ██║ ╚███╔╝ "
echo " ██║     ██╔══╝  ██║     ██║     ██╔══╝  ╚██╗ ██╔╝██║   ██║ ██╔██╗ "
echo " ╚██████╗███████╗███████╗███████╗███████╗ ╚████╔╝ ╚██████╔╝██╔╝ ██╗"
echo "  ╚═════╝╚══════╝╚══════╝╚══════╝╚══════╝  ╚═══╝   ╚═════╝ ╚═╝  ╚═╝"
echo -e "${NC}"
echo -e " ${GREEN}Web Frontend${NC} — Frontend: ${YELLOW}http://localhost:5274${NC} | API: ${YELLOW}http://localhost:7432${NC}"
echo ""

# ── Backend ───────────────────────────────────────────────────────────
if [[ "$1" != "--frontend-only" ]]; then
  echo -e "${CYAN}[backend]${NC} Setting up Python virtual environment..."
  cd "$BACKEND_DIR"

  if [[ ! -d ".venv" ]]; then
    python3 -m venv .venv
  fi

  source .venv/bin/activate
  pip install -q -r requirements.txt

  echo -e "${CYAN}[backend]${NC} Starting FastAPI on port 7432..."
  uvicorn main:app --host 0.0.0.0 --port 7432 --reload &
  BACKEND_PID=$!
  echo -e "${GREEN}[backend]${NC} PID $BACKEND_PID"
fi

# ── Frontend ──────────────────────────────────────────────────────────
if [[ "$1" != "--backend-only" ]]; then
  echo -e "${CYAN}[frontend]${NC} Starting Vite dev server on port 5274..."
  cd "$FRONTEND_DIR"
  npm run dev &
  FRONTEND_PID=$!
  echo -e "${GREEN}[frontend]${NC} PID $FRONTEND_PID"
fi

echo ""
echo -e " ${GREEN}✓ CellEvoX Web is running!${NC}"
echo -e "   Open: ${YELLOW}http://localhost:5274${NC}"
echo ""

# Trap Ctrl+C to kill both
trap "echo 'Shutting down...'; kill $BACKEND_PID $FRONTEND_PID 2>/dev/null; exit" SIGINT SIGTERM
wait
