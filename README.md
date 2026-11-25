# Filesystem-ADFS

Independent Acorn Advanced Disc Filing System (ADFS) provider for InfiltratorOS. This repository is reserved for the canonical C/C++ filesystem engine and its single distributable `ADFS.so`.

**Status: documentation only; source extraction pending.** The editable upstream-derived implementation remains in [Filesystems](https://github.com/Infiltrator-Projects/Filesystems/tree/101026021d9088d80b7f0f472d9061f0e2c688a3/native/filesystems/adfs). No independent driver, package, runtime capability or BOOT support is claimed.

## Common V2

The target interface is exclusively `infiltrator_filesystem_api_v2` from [Libraries](https://github.com/Infiltrator-Projects/Libraries). One native engine owns disc geometry, allocation maps, F/F+ directories, object addresses and RISC OS filetype metadata. Hosted and freestanding profiles must share this engine; no FUSE or separate filesystem driver is permitted.

## Build and install

Planned outputs: `/System/Filesystems/ADFS.so` and `/System/Resources/Documentation/Filesystem-ADFS.pdf`. Release assets are a generic amd64 `.deb`, source-downloading CPU-native `.run`, and GitHub-generated source ZIP. No build or installer exists yet.

See [migration status](MIGRATION_STATUS.md) and [engineering philosophy](docs/PHILOSOPHY.md).
