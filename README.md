# ZephyrDB Example

A standalone Zephyr application demonstrating
[ZephyrDB](https://github.com/beriberikix/zephyrdb) — an embedded multi-model
database for Zephyr RTOS.

This "kitchen sink" example exercises every major ZephyrDB feature in a single
app targeting `native_sim`:

- **KV** — set, get, delete, and iterate keys (ZMS backend)
- **Time-series** — append, batch append, flush, FlatBuffer export, and all five
  aggregate queries (MIN/MAX/AVG/SUM/COUNT)
- **Document** — create, set typed fields (string, i64, f64, bool, bytes), save,
  read back, FlatBuffer export, query with filters, delete
- **Eventing** — callback listeners and zbus channel reads for KV, TS, and DOC
  events
- **Shell** — interactive `zdb health` and `zdb stats` commands
- **Health & Stats** — query, export, validate, and reset statistics

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
├── prj.conf         # Kconfig: enables all ZephyrDB features for native_sim
├── src/
│   └── main.c       # Kitchen-sink demo application
├── west.yml         # West manifest with minimal dependencies
└── README.md
```

## West Manifest

The manifest pulls only what `native_sim` needs:

| Project | Purpose |
|---------|---------|
| `zephyr` | Zephyr RTOS (imports only `littlefs` from submanifest) |
| `zephyrdb` | ZephyrDB module |
| `flatcc-zephyr` | FlatBuffers Zephyr overlay |
| `flatcc` | Upstream FlatBuffers runtime (required by flatcc-zephyr) |

## License

Apache-2.0 — see [LICENSE](LICENSE).
