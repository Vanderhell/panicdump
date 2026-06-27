# panicdump

panicdump is a small retained-RAM crash dump library for bare-metal Cortex-M
systems with an offline decoder.

## What it does

- Captures fault state into a fixed 192-byte wire image
- Uses a CRC-32 over the final image, including the final commit magic
- Writes the commit marker last, with a publish barrier before the final store
- Exports the dump as hex text and decodes it offline with Python
- Supports a host/mock port for tests and a Cortex-M port for embedded builds

## Public API

```c
bool panicdump_has_valid(void);
const panicdump_dump_t *panicdump_get(void);
void panicdump_clear(void);

panicdump_export_status_t panicdump_export_hex(panicdump_write_char_fn write_char);
panicdump_export_status_t panicdump_boot_check_and_export(panicdump_write_char_fn write_char);

void panicdump_set_user_tag(uint32_t tag);
uint32_t panicdump_get_user_tag(void);

uint32_t panicdump_reason_tag_hash_n(const char *reason_tag, size_t max_len);
void panicdump_trigger_reason(uint32_t reason_tag) PANICDUMP_NORETURN;
void panicdump_trigger_tag(const char *reason_tag, size_t max_len) PANICDUMP_NORETURN;
```

`panicdump_trigger(const char *reason_tag)` is provided as a bounded wrapper
around `panicdump_trigger_tag(..., PANICDUMP_REASON_TAG_MAX_LEN)`.

## Build

Host build and tests:

```sh
cmake -S . -B build -DPANICDUMP_BUILD_TESTS=ON -DPANICDUMP_PORT=host
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Install and consume as a package:

```sh
cmake --install build --prefix .install
cmake -S tests/package_consumer -B build-consumer -DCMAKE_PREFIX_PATH=./.install
cmake --build build-consumer --config Release
```

## Decoder

```sh
python3 tools/decode_panicdump.py dump.bin
python3 tools/decode_panicdump.py --hex uart_capture.txt
python3 tools/decode_panicdump.py --json dump.bin
python3 tools/decode_panicdump.py --verify dump.bin
```

## Tests

Python host tests:

```sh
python3 -m unittest discover -s tests/host_format_tests -p "test_*.py"
```

Native C host smoke test:

```sh
ctest --test-dir build -C Release --output-on-failure
```

## Package layout

- `include/` public headers
- `src/` core and port sources
- `ports/cortexm/` Cortex-M assembly entry stubs
- `tools/` mock dump generator and decoder
- `tests/` Python and native host tests
