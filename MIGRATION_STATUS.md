# ADFS development qualification

Native FileCore new-map reader with bounded allocation fragments and free chains, zone and directory checksums, E/F and E+/F+ directories, shared fragments, nested files and measured free space. The checksummed map supplies disc identity and root length.

[Passing hosted, freestanding and sanitizer CI](https://github.com/Infiltrator-Projects/Filesystem-ADFS/actions/runs/45972969338) at `e2d325572dcbfcc53d7046fb4f0c56cd3d92a80b`.

Constructed fixtures and independently sourced physical Acorn E, E+, F and F+ images pass through both builds with 512/4096-byte host sectors; whole-image hashes remain unchanged. Old maps, non-ASCII names, independent payload oracle comparison and signed boot remain unqualified.

The read-only DEB and native RUN first shipped in v0.1.0. File extent and allocation queries are exercised on four physical Acorn images. Mutation, formatting, repair and relocation remain open. The BOOT development matrix separately tests its embedded firmware carrier and namespace reads; matrix inclusion alone is not a successful run or signed firmware boot evidence.
