# ADFS: technical reference

## Scope, lineage and media families

ADFS began on the BBC Micro family as a hierarchical alternative to DFS. The old-map S/M/L and later D media must not be conflated with the new-map E/F family or the later big-directory extension. Old-map allocation records describe contiguous free regions; new-map allocation identifies fragments, making noncontiguous files possible. A disk image must be interpreted with the exact sector ordering, side ordering and geometry of its media profile. An image-container header is outside the filesystem address space. This reference concentrates on RISC OS FileCore new-map media and documents old-map differences rather than pretending one decoder handles every variant.

The Archimedes and RISC OS generations extended the earlier BBC Micro design.

## Logical addresses and disk discovery

Distinguish image byte offsets, device transfer sectors, FileCore sectors and allocation-map bits. FileCore sectors have size 2^log2secsize bytes; each allocation map bit represents 2^log2bpmb bytes. Never assume either equals 512. Discover a candidate disc record using the format-specific boot block or map location; verify its checksum, geometry, declared size and corroborating map metadata before accepting the volume. The map may itself occupy more than one allocation zone. Translate a FileCore logical address only after deriving the layout for its exact media profile. Do not assume a physical floppy track order from a logical image.

## Disc record: initial fields

All offsets in this chapter are relative to the disc record, not the partition. Multi-byte quantities are little-endian. Byte 0: log2secsize; 1: sectors per track; 2: head count; 3: density; 4: allocation fragment ID width in bits; 5: log2 bytes per allocation-map bit; 6: track skew; 7: boot option; 8: low-sector flags; 9: low byte of zone count. Bytes 10-11: zone spare bits; 12-15: root indirect address; 16-19: lower 32 bits of disk byte size; 20-21: disk cycle ID; 22-31: ten-byte disk name. Bounds-check all shifts and products before computing addresses. A plausible name is not a signature.

## Disc record: large-media extension

Offsets 32-35 hold disk filetype; 36-39 extend disk size; 40 describes sharing granularity as a base-two exponent; 41 holds large-format flags; 42 extends the zone count; 43 is reserved in the cited specification; 44-47 identify format version; 48-51 give root-directory size; 52-59 are reserved. Combine the size halves in a wide unsigned integer only when the admitted revision defines the high half. Revisions differ in the use of flags and in whether a root size is explicit. The developer specification's big-directory changes must be read alongside the applicable FileCore revision, not applied indiscriminately to earlier media.

## New-map allocation model

A zone contains control/check data and a bitstream of variable-length fragments. The fragment identifier occupies the disc record's ID width; following zero bits extend the represented range until a terminating set bit. The extent length counts the complete representation in map bits, not only the zero run. Fragments with the same allocated ID can describe discontiguous storage belonging to one object or shared allocation group. A reserved ID and the zone's free-chain links identify free space; do not interpret a free-chain link as a file ID. Zone spare bits and the disc-record area are not allocatable payload. Validate each zone's check byte and cross-zone check according to the selected FileCore format. Scan bounded bit ranges; require terminators and reject cycles or a free-chain link that lands inside another fragment.

## Indirect addresses and a file read

An indirect address is not a raw sector number. Resolve its fragment ID through the map and apply the sharing-offset semantics to find the logical starting point within the selected allocation. The low address bits may encode a share offset; use the format revision and share_size when interpreting it. ID 2 identifies the root allocation in the retained reference implementation. Build a mapping from each admissible fragment ID to ordered physical runs using the map's prescribed zone search order, not an arbitrary sorted list. Read the object length from its directory record; walk the logical runs, clipping the final transfer to the byte length. Refuse missing runs, duplicate ownership outside legitimate sharing and lengths larger than the available mapped storage.

## Fixed directory records

The later fixed directory format has 26-byte records. Offsets relative to an entry: name at 0 (10 bytes); load at 10 (4); execution at 14 (4); length at 18 (4); indirect address at 22 (3); attributes at 25 (1). Fixed-format directories use matching leading and trailing master sequence numbers and format identifiers, a parent pointer and directory check byte. The later tail has a last-entry marker, reserved bytes, three-byte parent, title and directory name. The old format has different access-bit and address semantics: do not treat its 26-byte record as this layout solely because the record size matches. Names are native byte strings, not implicitly UTF-8; apply the specific RISC OS character and comparison rules.

## Big directories: F+

The big-directory header includes a leading sequence, revision fields, identifying marker, name length at 8, total directory length at 12, entry count at 16, allocated name-heap size at 20 and parent indirect address at 24. The directory's own name begins at 28, with terminator and word alignment. Each fixed-size entry is 28 bytes: load 0, execution 4, length 8, indirect address 12, attributes 16, name length 20, name-heap offset 24; each field is four bytes. The variable names occupy a separate heap. Validate all header and heap boundaries, entry count times 28, name lengths, word alignment, parent and final eight-byte tail. The head/tail markers and sequence must agree with the directory revision; apply the documented directory checksum. No name pointer may escape the declared heap.

## Native attributes and timestamps

RISC OS uses load and execution addresses as metadata, not Unix inode time and mode fields. A typed timestamp convention uses the high marker bits of the load address, a 12-bit filetype in load-address bits 19:8 and a 40-bit centisecond count based on 1900. Preserve the filetype, original untyped load/execute values, native owner/public access, directory and locking semantics independently of any host permission projection. Name suffixes added by a host to represent filetype are presentation conventions and must not silently become stored names. Enumerate a directory by its validated record count or terminator rather than treating unused record bytes as names.

## Mutation, formatting and recovery

An old-map file can grow only into suitable contiguous free storage or after relocation; a new-map object may acquire additional fragments subject to representability, free-link and sharing constraints. Write payload and flush before publishing a new directory reference; maintain coherent zone control/check bytes, directory sequence and checksum, and all shared allocation relationships. The exact original FileCore write ordering is revision-specific; this proposed safe implementation boundary is not a claim of native journalling. A formatter needs a verified media profile, boot/disc record, zone map and root structure; compare a blank image byte-for-byte at the structural level with independent native output. Repair should reconstruct ownership from validated directories and map chains, report ambiguity, and avoid reclaiming a still-referenced shared fragment.

## Worked analysis and qualification

For a hypothetical sector exponent of 9 and allocation-bit exponent of 10, each sector is 512 bytes and each allocation-map bit represents 1024 bytes. A valid 16-bit-long fragment representation covers 16384 bytes before any object-level sharing or metadata restrictions; this is an arithmetic example, not a prescribed profile. Test exact-sector and cross-sector reads, two fragment runs for a single object, malformed termination, duplicate IDs with and without legitimate sharing, fixed and big directories, parent cycles, truncated names, typed and untyped metadata, and contradictory disk sizes. Independent RISC OS images and original FileCore documentation are the oracle. Do not advertise a write or defrag operation until its independent interruption and recovery tests pass.

## Implementation and qualification boundary

The original on-disk specification is independent of the Infiltrator provider. Its sole public interface is Common V2 (https://github.com/Infiltrator-Projects/Libraries/blob/main/docs/FILESYSTEM_COMMON_ABI_V2.md). Discover operation capabilities at runtime; a format signature, source function or export is not proof of qualification. Preserve distinct unsupported-feature, corrupt-media, I/O and stale-handle results. Check host geometry, transfer bounds and overlapping sector updates; require exclusive unmounted ownership and durable flush before raw mutation. Test Media and Defragmenter use the same provider operations, not separate format parsers. Hosted and freestanding profiles, release packages, BOOT and kernel integration require separate exact-source evidence. Current implementation and installation status belongs to this repository's README and qualification records, not the format's historical chronology.

## References and unresolved details

Acorn developer specification: https://www.4corn.co.uk/archive/Acorn_Developer_Site/reference/ro4/api/html/filecore2.html . Read sections 7.2-7.5 for directory and disc-record layouts. RISC OS 3 Programmer's Reference Manual, volume 2, pages 2-199 to 2-209 and volume 5a, pages 5a-165 to 5a-168, describes the original FileCore format and extensions. Linux reader documentation: https://docs.kernel.org/6.13/filesystems/adfs.html ; implementation reference: https://github.com/torvalds/linux/tree/v6.12/fs/adfs . Exact boot-block checksum and zone-check algorithms, old-map offsets, fragment-zone search ordering and revision-specific collation must be verified against these original sources and independent images before independent write implementation. This manual is an expanded engineering reference, not an assertion of implementation or release qualification.

Linux map.c is evidence for zone validation and map traversal; compare the disc-record arithmetic, map ownership, F/F+ lookup and physical data reads in that order.

Retained original source provenance: https://github.com/Infiltrator-Projects/Filesystems/blob/101026021d9088d80b7f0f472d9061f0e2c688a3/native/filesystems/adfs/linux/adfs.h (blob prefix 223f0283d2).

### Physical allocation and file extents

The E/F and E+/F+ reader exports both Common V2 mapping operations. ALLOCATION.MAP starts at any partition-relative byte. It derives free runs from the checked zone free chains and marks the partition tail reserved. DEFRAG.MAP walks an object's fragments from its starting zone, applies its sharing offset, joins physically adjacent bytes and clips the last extent at EOF. The extent index addresses the resulting runs in logical file order. Neither operation moves storage. The hardware-image tests compare every mapped file byte with the bytes at its reported physical address.
