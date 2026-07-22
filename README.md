# ZephyrDB Example

A standalone Zephyr application demonstrating
[ZephyrDB](https://github.com/beriberikix/zephyrdb) — an embedded multi-model
database for Zephyr RTOS.

This example exercises ZephyrDB features on `native_sim`:

- **KV** — set, get, delete, and iterate keys (ZMS backend)
- **Time-series** — append, flush, and aggregate query (LittleFS backend)
- **Document** — create, set fields, save, and delete (LittleFS backend)
- **Eventing** — callback listeners and zbus channel reads for KV, TS, and DOC events
- **FlatBuffers** — stats export via the FlatCC runtime
- **Shell** — interactive `zdb health` and `zdb stats` commands
- **Health & Stats** — query, export, validate, and reset statistics

A `boards/native_sim.overlay` adds a 256 KB LittleFS partition on the
simulated flash and an fstab automount at `/lfs`, enabling the TS and DOC
backends without any manual mount step.

## Prerequisites

- [Zephyr SDK](https://docs.zephyrproject.org/latest/develop/getting_started/)
  installed and on your PATH
- [uv](https://docs.astral.sh/uv/) for Python environment management
- A Linux host (the `native_sim` board does not build on macOS or Windows)

Dependencies are pinned in `west.yml` (Zephyr v4.4.1 plus fixed zephyrdb and
flatcc-zephyr revisions) so builds are reproducible; update the pins
deliberately when moving to newer releases.

## Setup

```bash
# Create a Python venv and install west
uv venv .venv
source .venv/bin/activate
uv pip install west pyelftools jsonschema

# Initialize the west workspace and fetch dependencies
west init -l .
west update
```

## Build

```bash
west build -b native_sim -p
```

## Run

```bash
./build/zephyr/zephyr.exe
```

After the demo output finishes, the app enters an interactive shell. Try:

```
zdb health
zdb stats
```

## Project Structure

```
.
├── boards/
│   └── native_sim.overlay  # Flash partition + LittleFS fstab automount
├── CMakeLists.txt           # Zephyr app build config + flatcc-zephyr module
├── prj.conf                 # Kconfig: KV, TS, DOC, FlatBuffers, eventing, shell
├── src/
│   └── main.c               # Demo application
├── west.yml                 # West manifest (zephyr, zephyrdb, flatcc-zephyr)
└── README.md
```

## West Manifest

The manifest pulls the modules needed for `native_sim`:

| Project | Purpose |
|---------|----------|
| `zephyr` | Zephyr RTOS (with `littlefs` allowlisted) |
| `zephyrdb` | ZephyrDB module |
| `flatcc-zephyr` | FlatCC runtime for FlatBuffer support |

## License

Apache-2.0 — see [LICENSE](LICENSE).
