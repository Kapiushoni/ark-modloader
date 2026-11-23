"""Add a DT_NEEDED entry to an ELF shared library.

usage: python patch_needed.py <in.so> <out.so> <libname.so>
"""
import sys

import lief


def main() -> int:
    src, dst, needed = sys.argv[1:4]
    elf = lief.ELF.parse(src)
    if elf is None:
        print(f"failed to parse {src}")
        return 1
    if any(e.name == needed for e in elf.dynamic_entries if isinstance(e, lief.ELF.DynamicEntryLibrary)):
        print(f"{needed} already present")
    else:
        elf.add_library(needed)
    elf.write(dst)
    print("NEEDED:", [e.name for e in lief.ELF.parse(dst).dynamic_entries
                      if isinstance(e, lief.ELF.DynamicEntryLibrary)])
    return 0


if __name__ == "__main__":
    sys.exit(main())
