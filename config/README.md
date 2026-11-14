# Coop configurations

One header per coop in `coops/`, named after its id. Each one is a complete
`coop_config.h` (see `firmware/coop_node/coop_config_example.h` for every
option). They hold no secrets, so commit them.

```bash
scripts/build.sh coop_node --coop config/coops/coop-1.h --upload /dev/ttyUSB0
```

`build.sh` copies the file to `firmware/coop_node/coop_config.h`
(gitignored) before compiling.

| File | Coop |
|---|---|
| `coops/coop-1.h` | Template for real hardware (`MOCK_DOOR_ENABLED false`). Copy it per coop and change `COOP_ID` / `COOP_NAME` |

Ids must be unique on the farm and match `[a-z0-9][a-z0-9_-]{0,31}`. While
the server's simulator is on, avoid `main-coop` and `brooder`.
