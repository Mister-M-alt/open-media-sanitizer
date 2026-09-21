# CLI reference

[Documentation index](README.md) · [Operator guide](operator-guide.md)

The examples use `./build/oms` from a source checkout. On boot media or after
installation, use `oms`. `make cli` builds the backend without GTK dependencies.
The CLI runs on Linux and inspects the host's current mount namespace; do not
perform physical erasure inside a container with a partial view of storage.

## Commands

```text
oms list [--json]
oms inspect TARGET [--allow-file] [--json]
oms erase TARGET [OPTIONS]
oms --help
oms --version
```

`list` returns visible whole block devices. It omits partitions, loop, RAM, and
zram devices. `inspect` accepts an explicitly named target; regular files require
`--allow-file`. Both commands inspect without erasing. A whole-disk write also
overwrites the partition table and the exposed regions containing partitions.

```sh
./build/oms list
./build/oms list --json
./build/oms inspect /dev/sdX --json
```

`/dev/sdX` is a placeholder. Replace it with a device you have independently
identified. An erase command with no `--execute` shows a plan without writing.
Plans still refuse targets that are active, empty, read-only, or otherwise fail
validation.

```sh
./build/oms erase /dev/sdX --method zero --passes 1 --verify
```

## Erase options

| Option | Default | Meaning |
| --- | --- | --- |
| `--method zero\|ones\|random` | `zero` | Write zero bytes, `0xff` bytes, or bytes from `/dev/urandom` |
| `--passes NUMBER` | `1` | Integer from 1 to 16; each pass writes the entire exposed range |
| `--verify` | Off | Read and check the final zero/ones pattern; invalid with `random` |
| `--execute` | Off | Permit writes after confirmation and all usage/identity checks |
| `--confirm PATH` | Interactive prompt | Supply the exact canonical path for unattended confirmation |
| `--allow-file` | Off | Allow a regular file for controlled testing |
| `--expect-id ID` | Not supplied | Require the opaque identity from `inspect --json` to still match |
| `--progress` | Off | Emit progress events to standard error |
| `-h`, `--help` | — | Show usage |

Use `oms -V` or `oms --version` at the top level to print the version. The desktop
enables verification initially; the CLI requires `--verify` explicitly.

## Complete example using a temporary file

This example allocates a private temporary directory, writes only its newly
created 8 MiB regular file, verifies it, and removes the sample on exit. It
requires the normal Linux shell utilities used below, with no root privileges.

```sh
(
    set -eu
    oms_demo_dir=$(mktemp -d)
    oms_sample="$oms_demo_dir/sample.img"
    trap 'rm -f "$oms_sample"; rmdir "$oms_demo_dir"' EXIT
    truncate -s 8M "$oms_sample"
    oms_sample=$(readlink -f "$oms_sample")

    ./build/oms inspect "$oms_sample" --allow-file --json
    ./build/oms erase "$oms_sample" --allow-file --method zero --verify
    ./build/oms erase "$oms_sample" --allow-file --method zero --verify \
        --execute --confirm "$oms_sample" --progress
)
```

The first erase command prints **Dry run only**. The second writes and verifies,
then prints **Erase completed**. A dry run returns success too; it is not evidence
that writes occurred. File mode rejects multiple hard links, active swap files,
loop backing files, and conflicting advisory locks. It is for controlled tests,
not concurrent file shredding.

## Execute on identified physical storage

Read the [preparation guidance](operator-guide.md#prepare-for-a-real-operation)
and inspect the target first. The following command is destructive once you
enter its requested confirmation:

```sh
sudo ./build/oms erase /dev/sdX --method zero --verify --execute
```

The prompt requires the canonical path shown by inspection. There is no generic
`yes` answer. With non-interactive standard input, execution is refused unless
`--confirm` supplies that exact path. Confirmation does not bypass usage, root,
or identity checks.

For integrations, retain the `path` and `identity` returned by inspection. Pass
the approved path through `--confirm` and the captured identity through
`--expect-id`. Treat identity as an opaque string, not as a serial number or
permanent hardware identifier. If it changes, inspect and obtain a new
confirmation. Do not select and erase every result of `list` automatically.

## JSON inventory

`list --json` returns an array; `inspect --json` returns one object. It describes
inspection state, not an operation report.

| Field | Type | Meaning |
| --- | --- | --- |
| `path` | String | Canonical target path |
| `model` | String | Available model description; may be empty |
| `kind` | String | `block` or `file` |
| `size_bytes` | Integer | Exposed target size in bytes |
| `read_only` | Boolean | Target is read-only according to inspection |
| `mounted` | Boolean | Mounted, or mount usage could not be established safely |
| `swap_active` | Boolean | Active swap, or swap usage could not be established safely |
| `has_holders` | Boolean | Dependent usage, or required usage checks could not be completed safely |
| `removable` | Boolean | Kernel-provided removability flag; not a guarantee of safe removal |
| `identity` | String | Value for `--expect-id`; includes size and identity information |

Integrations should preserve integer precision for byte counts and treat missing
or invalid information as a reason to stop. Inventory can become stale; the
backend checks again before writing. This preview's inventory and progress
formats are not a versioned, stable public API.

## Output and exit status

Plans and normal messages go to standard output. Errors and progress go to
standard error. With `--progress`, event lines have this format:

```text
OMS_PROGRESS PHASE PASS PASSES BYTES_DONE BYTES_TOTAL
```

`PHASE` is `writing` or `verifying`. Byte counts apply to that phase and pass,
not the aggregate of all passes. Other diagnostic lines may appear on the same
stream. A 100% event does not prove successful final flushing or close: wait
for process exit. Without this flag, interactive standard error gets a human
percentage display; redirected output does not receive those events.

| Exit code | Meaning |
| --- | --- |
| `0` | Command succeeded; includes inspection and accepted dry runs |
| `1` | Runtime failure or refusal, including handled interruption during a write |
| `2` | Invalid command, arguments, or unsupported settings |

An externally killed process can have a signal-related status instead. Treat
any abnormal exit as unsuccessful. Ctrl+C or SIGTERM requests cancellation
during writes; cleanup attempts to flush already attempted writes. Blocking
device I/O can delay termination. Cancellation does not restore overwritten
data and there is no resume mode.

The CLI does not export the desktop's JSON/HTML operation records. Capture its
output and exit status if integrating it, and see [reports](reports.md) for the
desktop record format.
