"""Read-only file browsing for protocol v1 (/c64/v1/files/...).

Everything the C64 can reach is below one configured storage root. The
response formats are specified in docs/NET_PROTOCOL.md; the constants here
are the protocol limits and must match apps/02_netfiles.

Nothing in this module writes to the filesystem.
"""
import binascii
import codecs
import struct
import unicodedata
from pathlib import Path

PROTOCOL_VERSION = 1

# Limits (docs/NET_PROTOCOL.md)
PATH_MAX = 128          # bytes of a directory path, including the trailing "/"
NAME_MAX = 32           # bytes of one name
LIST_COUNT_MAX = 18     # entries per list response
TEXT_COUNT_MAX = 22     # lines per text response
LIST_BIG_MAX = 32       # entries per list response with big=1 (NetFiles/VGA: 23-row panels), and
TEXT_BIG_MAX = 32       # lines per text response with big=1 (docs/NET_PROTOCOL.md, XT additions 8)
TEXT_WIDTH = 40         # characters per text line (the default, and the C64's)
TEXT_WIDTH_MIN = 20     # an optional `width` parameter may ask for 20-80 (the XT's 80 columns)
TEXT_WIDTH_MAX = 80
TEXT_FILE_MAX = 1024 * 1024
TEXT_SNIFF_BYTES = 4096
TAB_WIDTH = 8
READ_CHUNK_MAX = 1000   # data bytes per read response
READ_BIG_MAX = 4096     # ... when the request says big=1 (the XT; docs/NET_PROTOCOL.md, XT additions)
DOWNLOAD_FILE_MAX = 16 * 1024 * 1024

# Entry types
TYPE_DIRECTORY, TYPE_TEXT, TYPE_PRG, TYPE_BINARY = 0, 1, 2, 3

# Response status codes (in the body; the HTTP status is 200)
STATUS_OK = 0
STATUS_NOT_FOUND = 1
STATUS_WRONG_TYPE = 2       # list on a file, text on a directory
STATUS_BAD_REQUEST = 3      # malformed parameters or path
STATUS_ACCESS_DENIED = 4    # resolves outside the storage root
STATUS_NOT_TEXT = 5
STATUS_TOO_LARGE = 6
STATUS_CHANGED = 7          # the file changed while it was being read

LIST_HEADER = struct.Struct("<BBHHBH")   # version, status, total, start, count, hidden
TEXT_HEADER = struct.Struct("<BBHHB")    # version, status, total lines, first line, count
INFO_REPLY = struct.Struct("<BBBIH")     # version, status, type, size, CRC-16
READ_HEADER = struct.Struct("<BBIIH")    # version, status, file size, offset, count
ENTRY_HEADER = struct.Struct("<BIB")     # type, size, name length


class RequestError(Exception):
    def __init__(self, status):
        super().__init__(status)
        self.status = status


def is_c64_name(name):
    """Names the C64 can display and send back: printable ASCII, no path
    separators, not hidden, 1..NAME_MAX bytes."""
    return (0 < len(name) <= NAME_MAX
            and all(0x20 <= ord(c) <= 0x7E for c in name)
            and "/" not in name and "\\" not in name
            and not name.startswith("."))


def resolve(root, relative_path):
    """Map a protocol path ("", "docs/", "docs/a.txt") to a filesystem path
    inside root. Raises RequestError."""
    if len(relative_path.encode("ascii", "replace")) > PATH_MAX + NAME_MAX:
        raise RequestError(STATUS_BAD_REQUEST)
    parts = relative_path.split("/")
    if parts and parts[-1] == "":
        parts.pop()                         # trailing "/" of a directory path
    if any(not is_c64_name(part) for part in parts):
        raise RequestError(STATUS_BAD_REQUEST)
    root_real = Path(root).resolve()
    target = root_real.joinpath(*parts)
    if not target.exists() and not target.is_symlink():
        raise RequestError(STATUS_NOT_FOUND)
    real = target.resolve()
    if real != root_real and root_real not in real.parents:
        raise RequestError(STATUS_ACCESS_DENIED)
    if not real.exists():
        raise RequestError(STATUS_NOT_FOUND)
    return real


def looks_like_text(path):
    with open(path, "rb") as f:
        head = f.read(TEXT_SNIFF_BYTES)
    if b"\0" in head:
        return False
    try:
        codecs.getincrementaldecoder("utf-8")().decode(head, final=False)
        return True
    except UnicodeDecodeError:
        return False


def entry_type(path):
    if path.is_dir():
        return TYPE_DIRECTORY
    if path.suffix.lower() == ".prg":
        return TYPE_PRG
    try:
        return TYPE_TEXT if looks_like_text(path) else TYPE_BINARY
    except OSError:
        return TYPE_BINARY


def visible_entries(root, directory):
    """Sorted (name, type, size) of entries the C64 may see, and the number
    of entries left out."""
    root_real = Path(root).resolve()
    entries, hidden = [], 0
    for child in directory.iterdir():
        name = child.name
        try:
            real = child.resolve()
            inside = real == root_real or root_real in real.parents
            if not is_c64_name(name) or not inside or not (real.is_dir() or real.is_file()):
                hidden += 1
                continue
            kind = entry_type(real)
            size = 0 if kind == TYPE_DIRECTORY else min(real.stat().st_size, 0xFFFFFFFF)
        except OSError:
            hidden += 1
            continue
        entries.append((name, kind, size))
    entries.sort(key=lambda e: (e[1] != TYPE_DIRECTORY, e[0].casefold(), e[0]))
    return entries, hidden


def list_response(root, relative_path, start, count, big=False):
    try:
        if count < 1 or count > (LIST_BIG_MAX if big else LIST_COUNT_MAX) or start > 0xFFFF:
            raise RequestError(STATUS_BAD_REQUEST)
        if relative_path and not relative_path.endswith("/"):
            raise RequestError(STATUS_BAD_REQUEST)
        if len(relative_path) > PATH_MAX:
            raise RequestError(STATUS_BAD_REQUEST)
        directory = resolve(root, relative_path)
        if not directory.is_dir():
            raise RequestError(STATUS_WRONG_TYPE)
        entries, hidden = visible_entries(root, directory)
    except RequestError as e:
        return LIST_HEADER.pack(PROTOCOL_VERSION, e.status, 0, start & 0xFFFF, 0, 0)
    total = min(len(entries), 0xFFFF)
    page = entries[start:start + count] if start < total else []
    body = bytearray(LIST_HEADER.pack(PROTOCOL_VERSION, STATUS_OK, total, start, len(page), min(hidden, 0xFFFF)))
    for name, kind, size in page:
        encoded = name.encode("ascii")
        body += ENTRY_HEADER.pack(kind, size, len(encoded)) + encoded
    return bytes(body)


def to_c64_ascii(line, keep=""):
    """Printable ASCII: accents dropped, tabs expanded, everything else "?" (except the characters in `keep`)."""
    decomposed = unicodedata.normalize("NFKD", line)
    out = ""
    for c in decomposed:
        if c == "\t":
            out += " " * (TAB_WIDTH - len(out) % TAB_WIDTH)
        elif unicodedata.combining(c):
            continue
        elif 0x20 <= ord(c) <= 0x7E or c in keep:
            out += c
        else:
            out += "?"
    return out


def wrap_line(line, width=TEXT_WIDTH):
    """Word-wrap one line; words longer than the width are split."""
    if not line:
        return [""]
    rows = []
    while len(line) > width:
        cut = line.rfind(" ", 0, width + 1)
        if cut <= 0:
            rows.append(line[:width])
            line = line[width:]
        else:
            rows.append(line[:cut])
            line = line[cut + 1:]
    rows.append(line)
    return rows


def valid_width(width):
    return TEXT_WIDTH_MIN <= width <= TEXT_WIDTH_MAX


def text_lines(path, width=TEXT_WIDTH):
    text = path.read_bytes().decode("utf-8", "replace")
    rows = []
    for line in text.splitlines():
        rows.extend(wrap_line(to_c64_ascii(line), width))
    return rows


def text_response(root, relative_path, first, count, width=TEXT_WIDTH, big=False):
    try:
        if count < 1 or count > (TEXT_BIG_MAX if big else TEXT_COUNT_MAX) or first > 0xFFFF or not valid_width(width):
            raise RequestError(STATUS_BAD_REQUEST)
        path = resolve(root, relative_path)
        if not path.is_file():
            raise RequestError(STATUS_WRONG_TYPE)
        if path.stat().st_size > TEXT_FILE_MAX:
            raise RequestError(STATUS_TOO_LARGE)
        if entry_type(path) != TYPE_TEXT:
            raise RequestError(STATUS_NOT_TEXT)
        rows = text_lines(path, width)
    except RequestError as e:
        return TEXT_HEADER.pack(PROTOCOL_VERSION, e.status, 0, first & 0xFFFF, 0)
    except OSError:
        return TEXT_HEADER.pack(PROTOCOL_VERSION, STATUS_NOT_FOUND, 0, first & 0xFFFF, 0)
    total = min(len(rows), 0xFFFF)
    page = rows[first:first + count] if first < total else []
    body = bytearray(TEXT_HEADER.pack(PROTOCOL_VERSION, STATUS_OK, total, first, len(page)))
    for row in page:
        encoded = row.encode("ascii")
        body += bytes([len(encoded)]) + encoded
    return bytes(body)


def list_response_max():
    return LIST_HEADER.size + LIST_COUNT_MAX * (ENTRY_HEADER.size + NAME_MAX)


def text_response_max(width=TEXT_WIDTH):
    return TEXT_HEADER.size + TEXT_COUNT_MAX * (1 + width)


def crc16(data, crc=0xFFFF):
    """CRC-16/CCITT-FALSE (polynomial $1021, initial $FFFF), as the C64 computes it."""
    return binascii.crc_hqx(data, crc)


def downloadable_file(root, relative_path):
    path = resolve(root, relative_path)
    if not path.is_file():
        raise RequestError(STATUS_WRONG_TYPE)
    if path.stat().st_size > DOWNLOAD_FILE_MAX:
        raise RequestError(STATUS_TOO_LARGE)
    return path


def info_response(root, relative_path):
    """Type, size and CRC-16 of a file, so the C64 can verify a download."""
    try:
        path = downloadable_file(root, relative_path)
        data = path.read_bytes()
    except RequestError as e:
        return INFO_REPLY.pack(PROTOCOL_VERSION, e.status, 0, 0, 0)
    except OSError:
        return INFO_REPLY.pack(PROTOCOL_VERSION, STATUS_NOT_FOUND, 0, 0, 0)
    if len(data) > DOWNLOAD_FILE_MAX:
        return INFO_REPLY.pack(PROTOCOL_VERSION, STATUS_TOO_LARGE, 0, 0, 0)
    return INFO_REPLY.pack(PROTOCOL_VERSION, STATUS_OK, entry_type(path), len(data), crc16(data))


def read_response(root, relative_path, offset, count, big=False):
    """Up to READ_CHUNK_MAX bytes (READ_BIG_MAX with big) of a file starting at offset."""
    try:
        if count < 1 or count > (READ_BIG_MAX if big else READ_CHUNK_MAX) or offset > 0xFFFFFFFF:
            raise RequestError(STATUS_BAD_REQUEST)
        path = downloadable_file(root, relative_path)
        with open(path, "rb") as f:
            f.seek(0, 2)
            size = f.tell()
            if offset > size:
                raise RequestError(STATUS_CHANGED)
            f.seek(offset)
            data = f.read(count)
    except RequestError as e:
        return READ_HEADER.pack(PROTOCOL_VERSION, e.status, 0, offset & 0xFFFFFFFF, 0)
    except OSError:
        return READ_HEADER.pack(PROTOCOL_VERSION, STATUS_NOT_FOUND, 0, offset & 0xFFFFFFFF, 0)
    return READ_HEADER.pack(PROTOCOL_VERSION, STATUS_OK, size, offset, len(data)) + data


def read_response_max():
    return READ_HEADER.size + READ_CHUNK_MAX


# --- Documents for the word processor (03_wordproc): doc-info, doc-read ----------

DOCUMENTS_FOLDER = "documents"
CODE_FOLDER = "code"             # CodeSmith's mirrors of programs: code/<project>/<file> (XD44)
PROJECT_EXTENSIONS = {".c": "c", ".h": "c", ".pas": "pascal", ".inc": "pascal"}
DOC_TEXT_MAX = 32 * 1024        # WordSmith/64's text buffer
DOC_TEXT_BIG_MAX = 60 * 1024    # ... WordSmith/XT's, when the request says big=1


STYLE_MARKS = "\x02\x19\x15"     # WordSmith/VGA's bold, italic and underline marks (marks=1)


def doc_text(path, marks=False):
    """The file as plain ASCII for a word processor: UTF-8 decoded, accents
    removed, line ends as LF, tabs expanded, anything else as "?" (with
    marks=1 the three style marks are kept)."""
    text = path.read_bytes().decode("utf-8", "replace")
    lines = text.replace("\r\n", "\n").replace("\r", "\n").split("\n")
    keep = STYLE_MARKS if marks else ""
    return "\n".join(to_c64_ascii(line, keep) for line in lines).encode("ascii")


def project_files(root, project):
    """The source files of code/<project>/ as [(name, text)]: the text as doc_text makes it (ASCII, LF, tabs expanded, the
    style marks kept), the names sorted headers first (.H, .INC), then the rest. Raises RequestError if the project is
    not a folder the C64 can name. Symlinks and anything that is not a small regular file are skipped."""
    if not is_c64_name(project):
        raise RequestError(STATUS_BAD_REQUEST)
    folder = resolve(root, CODE_FOLDER + "/" + project + "/")
    if not folder.is_dir():
        raise RequestError(STATUS_NOT_FOUND)
    found = []
    for child in folder.iterdir():
        if child.is_symlink() or not child.is_file() or child.suffix.lower() not in PROJECT_EXTENSIONS \
                or not is_c64_name(child.name) or child.stat().st_size > TEXT_FILE_MAX:
            continue
        try:
            found.append((child.name, doc_text(child).decode("ascii")))
        except OSError:
            continue
    found.sort(key=lambda f: (Path(f[0]).suffix.lower() not in (".h", ".inc"), f[0].casefold()))
    return found


def find_document(root, folder_path, name):
    """The file `name` in the folder; if there is no exact match, the one file
    whose name differs only in case (the C64 types names in one case)."""
    if folder_path and not folder_path.endswith("/"):
        raise RequestError(STATUS_BAD_REQUEST)
    if len(folder_path) > PATH_MAX or not is_c64_name(name):
        raise RequestError(STATUS_BAD_REQUEST)
    folder = resolve(root, folder_path)
    if not folder.is_dir():
        raise RequestError(STATUS_WRONG_TYPE)
    exact = folder / name
    if exact.exists() or exact.is_symlink():
        return resolve(root, folder_path + name)
    matches = [child.name for child in folder.iterdir()
               if child.name.casefold() == name.casefold() and is_c64_name(child.name)]
    if len(matches) != 1:
        raise RequestError(STATUS_NOT_FOUND)
    return resolve(root, folder_path + matches[0])


def document(root, folder_path, name, big=False, marks=False):
    path = find_document(root, folder_path, name)
    if not path.is_file():
        raise RequestError(STATUS_WRONG_TYPE)
    if path.stat().st_size > TEXT_FILE_MAX:
        raise RequestError(STATUS_TOO_LARGE)
    if entry_type(path) != TYPE_TEXT:
        raise RequestError(STATUS_NOT_TEXT)
    data = doc_text(path, marks)
    if len(data) > (DOC_TEXT_BIG_MAX if big else DOC_TEXT_MAX):
        raise RequestError(STATUS_TOO_LARGE)
    return data


def doc_info_response(root, folder_path, name, big=False, marks=False):
    try:
        data = document(root, folder_path, name, big, marks)
    except RequestError as e:
        return INFO_REPLY.pack(PROTOCOL_VERSION, e.status, 0, 0, 0)
    except OSError:
        return INFO_REPLY.pack(PROTOCOL_VERSION, STATUS_NOT_FOUND, 0, 0, 0)
    return INFO_REPLY.pack(PROTOCOL_VERSION, STATUS_OK, TYPE_TEXT, len(data), crc16(data))


def doc_read_response(root, folder_path, name, offset, count, big=False, marks=False):
    try:
        if count < 1 or count > (READ_BIG_MAX if big else READ_CHUNK_MAX):
            raise RequestError(STATUS_BAD_REQUEST)
        data = document(root, folder_path, name, big, marks)
        if offset > len(data):
            raise RequestError(STATUS_CHANGED)
    except RequestError as e:
        return READ_HEADER.pack(PROTOCOL_VERSION, e.status, 0, offset & 0xFFFFFFFF, 0)
    except OSError:
        return READ_HEADER.pack(PROTOCOL_VERSION, STATUS_NOT_FOUND, 0, offset & 0xFFFFFFFF, 0)
    chunk = data[offset:offset + count]
    return READ_HEADER.pack(PROTOCOL_VERSION, STATUS_OK, len(data), offset, len(chunk)) + chunk
