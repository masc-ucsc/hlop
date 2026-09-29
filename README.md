# Hardware Logic Operation Library (hlop)

This is a library to model logic operations performed by hardware compilers and simulators.


The semantics are set for LiveHD/Pyrope which uses unlimited precision signed
arithmetic, but it can be used by other hardware platforms.



## Memory image loading

All static `Memory_*` ordering variants and `Mem_dyn` support
`readmemh(const std::string& file, bool unknown_zero = false)` and `readmemb(...)`.
Load after configuring/initializing storage and before normal cycle evaluation.
The methods replace committed entries named in the file, retain unlisted entries,
and leave staged writes alone. Normal writes continue through `stage_write`/`tick`.

The parser accepts whitespace-separated words, underscores, `//` and `/* */`
comments, and hexadecimal `@address` directives in either format. Words are
truncated or zero-extended to the memory width. Dlop retains X/Z/? as unknown
bits; Slop uses seeded unknown bits unless `unknown_zero` requests zero.
Missing/malformed files and invalid addresses throw `std::runtime_error` with
the filename. Parsing completes before any entries change.
