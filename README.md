# Filesystem-ADFS

## 1. Purpose and format scope

Acorn Advanced Disc Filing System (ADFS). This repository is the designated independent implementation home for InfiltratorOS. **Read-only Common V2 provider for FileCore new-map media.**

## 2. On-disk architecture and invariants

Disc records establish geometry; allocation-map zones describe fragments; F/F+ directory formats and indirect disc addresses require separate decoding. RISC OS metadata must not be discarded or reinterpreted as Unix permissions. The initial reader implements the subset described below.

## 3. Common V2 implementation and capabilities

Native FileCore new-map reader with bounded allocation fragments and free chains, zone and directory checksums, E/F and E+/F+ directories, shared fragments, nested files and measured free space. The checksummed map supplies disc identity and root length. Hosted and import-free freestanding builds share the native engine and export `infiltrator_filesystem_api_v2`.

## 4. Integrity, mutation, allocation and recovery

The provider reports whole-partition allocation and file extents through Common V2. The E/F and E+/F+ tests cover fragmented and shared objects. Formatting, relocation, repair and durable mutation remain outstanding.

## 5. Build, installation and release

The generic AMD64 DEB installs `/System/Filesystems/ADFS.so` and `/System/Resources/Documentation/Filesystem-ADFS.pdf`. The native RUN rebuilds from pinned source. `make check` tests the hosted carrier and freestanding image.

## 6. Qualification and limitations

[Passing exact-revision CI](https://github.com/Infiltrator-Projects/Filesystem-ADFS/actions/runs/45972969338) at `e2d325572dcbfcc53d7046fb4f0c56cd3d92a80b`. Constructed fixtures and independently sourced physical Acorn E, E+, F and F+ images pass through both builds with 512/4096-byte host sectors; whole-image hashes remain unchanged. Old maps, non-ASCII names, independent payload oracle comparison and signed boot remain unqualified. Write, format, repair and relocation remain outstanding.

## 7. Technical references and provenance

- [Technical reference (PDF)](docs/ADFS.pdf), [editable reference](docs/ADFS-TECHNICAL-REFERENCE.md) and [PDF page source](docs/ADFS.pdfstreams) — one original-format technical reference; not an operational manual.

- [Pinned central source baseline](https://github.com/Infiltrator-Projects/Filesystems/tree/101026021d9088d80b7f0f472d9061f0e2c688a3/native/filesystems/adfs)
- [Native design](native/filesystems/adfs/DESIGN.md)
- [Migration and qualification plan](MIGRATION_STATUS.md)
- [Project philosophy](docs/PHILOSOPHY.md)
- [Common ABI](https://github.com/Infiltrator-Projects/Libraries)

Preserve inherited notices and licences on source extraction; new project-authored code uses GPL-3.0-or-later. Installation remains pending. Central code or test results are not evidence of an independent provider.
