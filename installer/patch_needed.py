"""Add (or remove) a DT_NEEDED entry in an ELF shared library.

usage: python patch_needed.py <in.so> <out.so> <libname.so> [--remove]
"""
import sys

import lief


def needed(elf):
    return [e.name for e in elf.dynamic_entries if isinstance(e, lief.ELF.DynamicEntryLibrary)]


def main() -> int:
    if len(sys.argv) < 4:
        print(__doc__)
        return 2
    src, dst, lib = sys.argv[1:4]
    remove = "--remove" in sys.argv[4:]
    elf = lief.ELF.parse(src)
    if elf is None:
        print(f"failed to parse {src}")
        return 1
    present = lib in needed(elf)
    if remove and present:
        elf.remove_library(lib)
    elif not remove and not present:
        elf.add_library(lib)
    elf.write(dst)
    print("NEEDED:", needed(lief.ELF.parse(dst)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
