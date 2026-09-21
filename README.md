# Open Media Sanitizer

Open Media Sanitizer (`oms`) is a small Linux command-line tool for overwriting an explicitly selected block device. It is designed around reviewable operations: inspection is separate from erasure, every erase starts as a dry run, and writes require both `--execute` and an exact target confirmation.

The project is at an early stage. Use it only after reviewing the source and testing the workflow in an environment where data loss is acceptable.

## Safety model

- There is no command that erases every detected device.
- `oms erase` is a dry run unless `--execute` is present.
- Execution requires the canonical target path to be typed interactively or supplied with `--confirm`.
- Mounted devices, active swap devices, read-only devices, and devices with active holders are rejected.
- The target is inspected again after confirmation and checked again after opening.
- Regular files are rejected unless `--allow-file` is supplied for controlled testing.
- Block-device execution requires root privileges.

These checks reduce operator error, but they cannot make a destructive command risk-free. Confirm the device identity, size, model, and mount state independently before executing an erase.

## Build and test

The build requires a C11 compiler, GNU Make, and Linux kernel headers.

```sh
make
make test
```

Install the executable as `/usr/local/bin/oms`:

```sh
sudo make install
```

## Usage

List visible whole block devices:

```sh
oms list
```

Inspect one target without writing to it:

```sh
oms inspect /dev/sdX
```

Review an erase plan. This command does not write data:

```sh
sudo oms erase /dev/sdX --method zero --verify
```

Execute the reviewed plan with interactive confirmation:

```sh
sudo oms erase /dev/sdX --method zero --verify --execute
```

For a non-interactive environment, `--confirm` must exactly match the canonical path printed by `inspect`:

```sh
sudo oms erase /dev/sdX --method zero --verify --execute --confirm /dev/sdX
```

Regular-file operation exists so the complete write path can be tested without a block device:

```sh
truncate -s 64M sample.img
oms erase sample.img --allow-file --method zero
oms erase sample.img --allow-file --method zero --verify --execute \
  --confirm "$(readlink -f sample.img)"
```

Run `oms --help` for all options.

## Methods

| Method | Written bytes | Read-back verification |
| --- | --- | --- |
| `zero` | `0x00` | Available |
| `ones` | `0xff` | Available |
| `random` | Bytes from `/dev/urandom` | Not available |

One pass is the default. Up to 16 passes can be requested, although additional host writes are not automatically more effective for every storage technology.

## Scope and limitations

`oms` performs sequential writes through the operating system to the logical address range exposed by a block device. It does not issue ATA Secure Erase, NVMe Sanitize, SCSI sanitize, cryptographic key-destruction, or vendor-specific firmware commands. It cannot directly overwrite remapped sectors, reserved capacity, overprovisioned flash, or inaccessible regions.

Read-back verification checks that the requested byte pattern can be read from the exposed logical range after the final pass. It is an integrity check for that operation, not a certification of sanitization or regulatory compliance.

The current implementation supports Linux. Device inventory intentionally omits loop, RAM, and zram devices, while an explicitly named regular file can be used with `--allow-file`.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). Security-sensitive reports should follow [SECURITY.md](SECURITY.md).

## License

Licensed under either of the following, at your option:

- Apache License, Version 2.0 ([LICENSE-APACHE](LICENSE-APACHE))
- MIT License ([LICENSE-MIT](LICENSE-MIT))

Unless you explicitly state otherwise, contributions intentionally submitted for inclusion in this project are licensed under the same terms.
