# ZephyrDB Example

A standalone Zephyr application demonstrating
[ZephyrDB](https://github.com/beriberikix/zephyrdb) — an embedded multi-model
database for Zephyr RTOS.

This example exercises the ZephyrDB features that work out-of-the-box on
`native_sim`:

- **KV** — set, get, delete, and iterate keys (ZMS backend)
- **Eventing** — callback listeners and zbus channel reads for KV events
- **Shell** — interactive `zdb health` and `zdb stats` commands
- **Health & Stats** — query, export, validate, and reset statistics

> **Note:** Time-series, Document, and FlatBuffer features require a mounted
> LittleFS filesystem and are not included in this initial example. A future
> release will add a `boards/native_sim.overlay` with a flash partition and
> LittleFS fstab entry to enable these features
> ([#2](https://github.com/beriberikix/zephyrdb-example/issues/2)). See the
> in-tree [zephyrdb samples](https://github.com/beriberikix/zephyrdb/tree/main/samples)
> for demonstrations of those features on hardware targets.

## Prerequisites

- [Zephyr SDK](https://docs.zephyrproject.org/latest/develop/getting_started/)
  installed and on your PATH
- [uv](https://docs.astral.sh/uv/) for Python environment management

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
├── CMakeLists.txt   # Zephyr app build configuration
├── prj.conf         # Kconfig: enables KV, eventing, shell, stats
├── src/
│   └── main.c       # Demo application
├── west.yml         # West manifest with minimal dependencies
└── README.md
```

## West Manifest

The manifest pulls only what `native_sim` needs:

| Project | Purpose |
|---------|---------|
| `zephyr` | Zephyr RTOS |
| `zephyrdb` | ZephyrDB module |

## License

Apache-2.0 — see [LICENSE](LICENSE).
