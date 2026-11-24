"""Zip a directory with forward-slash paths and Unix permissions (works the same on Windows and Linux).

usage: python zipdir.py <src_dir> <out.zip> [--prefix NAME]
  --prefix NAME  store entries under NAME/ (default: no prefix, the directory's contents at the root)
Shell scripts and update-binary get mode 755; text files that must stay LF are checked for CR.
"""
import os
import sys
import zipfile

LF_ONLY = (".sh", ".prop", "update-binary", "updater-script")


def main() -> int:
    args = sys.argv[1:]
    prefix = ""
    if "--prefix" in args:
        i = args.index("--prefix")
        prefix = args[i + 1].strip("/") + "/"
        del args[i:i + 2]
    if len(args) != 2:
        print(__doc__)
        return 2
    src, dst = args
    with zipfile.ZipFile(dst, "w", zipfile.ZIP_DEFLATED) as z:
        for root, dirs, files in os.walk(src):
            dirs.sort()
            for f in sorted(files):
                p = os.path.join(root, f)
                arc = prefix + os.path.relpath(p, src).replace(os.sep, "/")
                data = open(p, "rb").read()
                if arc.endswith(LF_ONLY) and b"\r" in data:
                    print(f"{arc}: has CRLF line endings")
                    return 1
                info = zipfile.ZipInfo(arc, date_time=(2026, 1, 1, 0, 0, 0))
                info.compress_type = zipfile.ZIP_DEFLATED
                info.external_attr = (0o100755 if arc.endswith((".sh", "update-binary")) else 0o100644) << 16
                z.writestr(info, data)
    print(dst)
    return 0


if __name__ == "__main__":
    sys.exit(main())
