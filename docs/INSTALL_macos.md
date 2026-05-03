# macOS (Apple Silicon) — ns-3 + NetAnim Installation

## Prerequisites

| Requirement | Notes |
|---|---|
| macOS 13 Ventura or later | Tested on Apple Silicon (M1/M2/M3) |
| Xcode Command Line Tools | Script checks and prompts if missing |
| Homebrew | Install from <https://brew.sh> if not present |
| Internet access | To download the ns-3 tarball and clone NetAnim |

> **Intel Mac users:** The script auto-detects `/usr/local` as the Homebrew prefix and works unchanged.

## Running the Script

```bash
# Clone this repo (if you haven't already)
git clone <repo-url>
cd <repo-dir>

# Run with default ns-3 version (3.42)
bash scripts/setup_macos_m2.sh

# Or specify a different version
bash scripts/setup_macos_m2.sh 3.43
```

The script is **idempotent** — re-running it safely skips already-completed steps.

## What Gets Installed

| Item | Location |
|---|---|
| ns-3 source + build | `~/ns3-workspace/ns-allinone-3.42/ns-3.42/` |
| NetAnim binary | `~/ns3-workspace/netanim/NetAnim` |
| Homebrew packages | `/opt/homebrew/` (arm64) |

Homebrew packages installed: `cmake`, `ninja`, `python@3.11`, `gsl`, `boost`, `sqlite`, `pkg-config`, `gnuplot`, `libxml2`, `graphviz`, `qt@5`.

## Recommended Shell Profile Exports

Add these lines to `~/.zshrc` (or `~/.bash_profile`):

```bash
export NS3_HOME="$HOME/ns3-workspace/ns-allinone-3.42/ns-3.42"
export PATH="/opt/homebrew/opt/qt@5/bin:$PATH"
export PKG_CONFIG_PATH="/opt/homebrew/opt/qt@5/lib/pkgconfig:$PKG_CONFIG_PATH"
```

Then reload: `source ~/.zshrc`

## Verifying the Installation

```bash
cd $NS3_HOME
./ns3 run hello-simulator          # should print "Hello Simulator"
~/ns3-workspace/netanim/NetAnim    # opens the NetAnim GUI
```

## Build Details

- **Compiler:** Apple Clang (native arm64, no cross-compilation)
- **Build profile:** `optimized` (no debug symbols, faster runtime)
- **Python bindings:** disabled (`--disable-python`) — pure C++ workflow
- **Modules enabled:** `core`, `network`, `internet`, `point-to-point`, `applications`, `flow-monitor`, `netanim`, `traffic-control`
