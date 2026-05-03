#!/usr/bin/env bash
# setup_macos_m2.sh — Native arm64 ns-3 + NetAnim installer for Apple Silicon macOS
# Usage: bash scripts/setup_macos_m2.sh [NS3_VERSION]
# Default NS3_VERSION=3.42

set -euo pipefail

NS3_VERSION="${1:-3.42}"
WORKSPACE="$HOME/ns3-workspace"
ALLINONE_DIR="$WORKSPACE/ns-allinone-${NS3_VERSION}"
NS3_DIR="$ALLINONE_DIR/ns-${NS3_VERSION}"
NETANIM_DIR="$WORKSPACE/netanim"
TARBALL="ns-allinone-${NS3_VERSION}.tar.bz2"
TARBALL_URL="https://www.nsnam.org/releases/${TARBALL}"

log() { printf '\n==> %s\n' "$*"; }

# ── 1. Xcode Command Line Tools ───────────────────────────────────────────────
log "Checking Xcode Command Line Tools"
if ! xcode-select -p &>/dev/null; then
    printf 'Xcode Command Line Tools not found. Launching installer...\n'
    xcode-select --install || true
    printf '\nWaiting for Xcode CLT installation to complete.\n'
    printf 'Re-run this script after the installer finishes.\n'
    exit 1
fi
printf 'Xcode CLT: %s\n' "$(xcode-select -p)"

# ── 2. Homebrew prefix detection ──────────────────────────────────────────────
log "Detecting Homebrew prefix"
if [[ -x /opt/homebrew/bin/brew ]]; then
    BREW_PREFIX=/opt/homebrew          # Apple Silicon
elif [[ -x /usr/local/bin/brew ]]; then
    BREW_PREFIX=/usr/local              # Intel Mac
else
    printf 'Homebrew not found. Install it from https://brew.sh and re-run.\n'
    exit 1
fi
export PATH="$BREW_PREFIX/bin:$PATH"
printf 'Homebrew prefix: %s\n' "$BREW_PREFIX"

# ── 3. Homebrew packages ──────────────────────────────────────────────────────
log "Installing Homebrew dependencies"
BREW_PKGS=(cmake ninja python@3.11 gsl boost sqlite pkg-config gnuplot libxml2 graphviz qt@5)
for pkg in "${BREW_PKGS[@]}"; do
    if brew list --formula "$pkg" &>/dev/null; then
        printf '  [skip] %s already installed\n' "$pkg"
    else
        printf '  [install] %s\n' "$pkg"
        brew install "$pkg"
    fi
done

# qt@5 is keg-only — expose it to the environment
QT5_PREFIX="$BREW_PREFIX/opt/qt@5"
export PATH="$QT5_PREFIX/bin:$PATH"
export PKG_CONFIG_PATH="$QT5_PREFIX/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
export LDFLAGS="-L$QT5_PREFIX/lib ${LDFLAGS:-}"
export CPPFLAGS="-I$QT5_PREFIX/include ${CPPFLAGS:-}"

# libxml2 is also keg-only on some systems
XML2_PREFIX="$BREW_PREFIX/opt/libxml2"
export PKG_CONFIG_PATH="$XML2_PREFIX/lib/pkgconfig:$PKG_CONFIG_PATH"

# ── 4. Download & extract ns-allinone tarball ─────────────────────────────────
log "Preparing ns-3 workspace ($WORKSPACE)"
mkdir -p "$WORKSPACE"

if [[ -d "$NS3_DIR" ]]; then
    printf '  [skip] %s already exists\n' "$NS3_DIR"
else
    cd "$WORKSPACE"
    if [[ ! -f "$TARBALL" ]]; then
        printf '  Downloading %s\n' "$TARBALL_URL"
        curl -L --progress-bar -o "$TARBALL" "$TARBALL_URL"
    else
        printf '  [skip] Tarball already downloaded\n'
    fi
    printf '  Extracting...\n'
    tar -xjf "$TARBALL"
fi

# ── 5. Configure & build ns-3 ─────────────────────────────────────────────────
log "Configuring ns-3 ${NS3_VERSION}"
cd "$NS3_DIR"

if [[ -f "build/lib/libns3-dev-core-optimized.dylib" || \
      -f "build/lib/libns3.${NS3_VERSION}-core-optimized.dylib" ]]; then
    printf '  [skip] ns-3 already built\n'
else
    ./ns3 configure \
        --enable-examples \
        --enable-tests \
        "--enable-modules=core,network,internet,point-to-point,applications,flow-monitor,netanim,traffic-control" \
        --build-profile=optimized \
        --disable-python

    log "Building ns-3 (using $(sysctl -n hw.ncpu) cores)"
    ./ns3 build -j"$(sysctl -n hw.ncpu)"
fi

# ── 6. Clone & build NetAnim ──────────────────────────────────────────────────
log "Setting up NetAnim"
if [[ -d "$NETANIM_DIR" ]]; then
    printf '  [skip] NetAnim directory already exists (%s)\n' "$NETANIM_DIR"
else
    git clone https://gitlab.com/nsnam/netanim.git "$NETANIM_DIR"
fi

NETANIM_BINARY="$NETANIM_DIR/NetAnim"
if [[ -f "$NETANIM_BINARY" ]]; then
    printf '  [skip] NetAnim binary already built\n'
else
    cd "$NETANIM_DIR"
    log "Building NetAnim"
    qmake NetAnim.pro
    make -j"$(sysctl -n hw.ncpu)"
fi

# ── 7. Link multi_as scratch project ─────────────────────────────────────────
log "Linking scratch/multi_as into ns-3 tree"
REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SCRATCH_SRC="$REPO_DIR/scratch/multi_as"
SCRATCH_DST="$NS3_DIR/scratch/multi_as"

if [[ -L "$SCRATCH_DST" ]]; then
    printf '  [skip] Symlink already exists: %s\n' "$SCRATCH_DST"
elif [[ -d "$SCRATCH_DST" ]]; then
    printf '  [skip] Directory already exists (not a symlink): %s\n' "$SCRATCH_DST"
else
    ln -s "$SCRATCH_SRC" "$SCRATCH_DST"
    printf '  Linked %s -> %s\n' "$SCRATCH_DST" "$SCRATCH_SRC"
fi

# Rebuild to include the new scratch target
log "Building multi_as_sim"
cd "$NS3_DIR"
./ns3 build multi_as_sim -j"$(sysctl -n hw.ncpu)"

# ── 8. Health check ───────────────────────────────────────────────────────────
log "Running health check: ./ns3 run hello-simulator"
cd "$NS3_DIR"
./ns3 run hello-simulator

log "Running multi_as_sim smoke test"
./ns3 run "multi_as_sim --nodes=20 --dist=balanced --scenarioId=smoke --runId=1 --simTime=1.0"

# ── Done ──────────────────────────────────────────────────────────────────────
cat <<EOF

════════════════════════════════════════════════════════════
  Installation complete!

  ns-3 home : $NS3_DIR
  NetAnim   : $NETANIM_BINARY

  Add to your shell profile (~/.zshrc or ~/.bash_profile):

    export NS3_HOME="$NS3_DIR"
    export PATH="$QT5_PREFIX/bin:\$PATH"
    export PKG_CONFIG_PATH="$QT5_PREFIX/lib/pkgconfig:\$PKG_CONFIG_PATH"

════════════════════════════════════════════════════════════
EOF
