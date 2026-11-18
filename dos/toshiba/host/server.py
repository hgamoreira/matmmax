#!/usr/bin/env python3
"""C64 Workstation host server.

Serves protocol v1 (docs/NET_PROTOCOL.md):
  /c64/v1/diag/...           fixed diagnostic content (01_wic64_diag)
  /c64/v1/files/...          browsing and downloading below one storage root
  /c64/v1/files/upload-...   uploads into the storage root (uploads.py)
  /c64/v1/files/rename, delete   rename, and delete into a trash (changes.py)
  /c64/v1/chat-...           chat with a local LLM through Ollama (chat.py)
  /c64/v1/ai-...             AI writing help for WordSmith, same model (writer.py)

Only the storage root is visible, and nothing outside it, even through
symlinks. Files change only through uploads.py and changes.py; delete moves
to a hidden trash folder and never erases. It never runs commands.

    python3 host/server.py                          # root: host/sample_root
    python3 host/server.py --root ~/c64-files --port 8064

Every request is logged with the client address and User-Agent. The WiC64
firmware sends "WiC64/<firmware version> (ESP32)", so the log also shows
which firmware made the request.
"""
import argparse
import logging
import re
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs

import changes
import chat
import files
import uploads
import writer

PROTOCOL_VERSION = 1
DEFAULT_PORT = 8064
DIAG_PREFIX = "/c64/v%d/diag/" % PROTOCOL_VERSION
FILES_PREFIX = "/c64/v%d/files/" % PROTOCOL_VERSION
CHAT_PREFIX = "/c64/v%d/chat-" % PROTOCOL_VERSION
AI_PREFIX = "/c64/v%d/ai-" % PROTOCOL_VERSION
DEFAULT_ROOT = Path(__file__).resolve().parent / "sample_root"
RAW_CONTENT_TYPE = "application/octet-stream"   # a POST with this type carries the raw chunk
HEX_PARAMETER = re.compile(r"[0-9A-Fa-f]{1,4}\Z")
HEX_OFFSET = re.compile(r"[0-9A-Fa-f]{1,8}\Z")

# Body the C64 compares byte for byte. Printable ASCII only (docs/NET_PROTOCOL.md).
HELLO_BODY = b"HELLO FROM C64 WORKSTATION HOST, PROTOCOL 1"
OVERSIZE_LENGTH = 1000   # larger than the C64's 256-byte receive limit
SLOW_REPLY_BODY = b"SLOW REPLY"

log = logging.getLogger("c64host")


def oversize_body():
    """1000 bytes: repeating "0123456789", so truncation is easy to see."""
    return (b"0123456789" * (OVERSIZE_LENGTH // 10 + 1))[:OVERSIZE_LENGTH]


class Handler(BaseHTTPRequestHandler):
    server_version = "C64WorkstationHost/%d" % PROTOCOL_VERSION
    protocol_version = "HTTP/1.1"

    def do_GET(self):
        path, _, query = self.path.partition("?")
        if path.startswith(FILES_PREFIX + "upload-"):
            if path == FILES_PREFIX + "upload-data":     # data only by POST
                self.reply(404, b"NOT FOUND")
            else:
                self.upload_request(path[len(FILES_PREFIX):], query)
            return
        if path.startswith(CHAT_PREFIX):
            self.chat_request(path[len(CHAT_PREFIX):], query)
            return
        if path.startswith(AI_PREFIX) and path != AI_PREFIX + "data":     # data only by POST
            self.ai_request(path[len(AI_PREFIX):], query)
            return
        if path in (FILES_PREFIX + "rename", FILES_PREFIX + "delete"):
            self.change_request(path[len(FILES_PREFIX):], query)
            return
        if path.startswith(FILES_PREFIX):
            self.files_request(path[len(FILES_PREFIX):], query)
            return
        name = path[len(DIAG_PREFIX):] if path.startswith(DIAG_PREFIX) else None
        if name == "hello":
            self.reply(200, HELLO_BODY)
        elif name == "oversize":
            self.reply(200, oversize_body())
        elif name == "fail":
            self.reply(500, b"INTERNAL ERROR (TEST)")
        elif name == "slow":
            time.sleep(self.server.slow_seconds)
            self.reply(200, SLOW_REPLY_BODY)
        else:
            self.reply(404, b"NOT FOUND")

    def query_params(self, query):
        try:
            return parse_qs(query, keep_blank_values=True, strict_parsing=bool(query),
                            encoding="ascii", errors="strict")
        except (ValueError, UnicodeDecodeError):
            return None

    @staticmethod
    def single(params, *keys):
        """The values of exactly these keys (each given once), or None."""
        if params is None or set(params) != set(keys) or any(len(params[k]) != 1 for k in keys):
            return None
        return [params[k][0] for k in keys]

    @staticmethod
    def optional_width(params):
        """The `width` parameter (hex, 20-80; default 40) when it is the only one given, else 0."""
        if params == {}:
            return files.TEXT_WIDTH
        values = Handler.single(params, "width")
        if values is None or not HEX_PARAMETER.match(values[0]):
            return 0
        width = int(values[0], 16)
        return width if files.valid_width(width) else 0

    @staticmethod
    def optional_big(params):
        """The optional `big=1` (XT): True if given as exactly 1, False if absent, None if anything else."""
        if params is None or "big" not in params:
            return False
        return True if params["big"] == ["1"] else None

    @staticmethod
    def optional_marks(params):
        """The optional `marks=1` (WordSmith/VGA): the same rule as big=1."""
        if params is None or "marks" not in params:
            return False
        return True if params["marks"] == ["1"] else None

    def upload_request(self, name, query, body=None, chunk_max=uploads.UPLOAD_CHUNK_MAX):
        params = self.query_params(query)
        up = self.server.uploads
        bad = uploads.reply(files.STATUS_BAD_REQUEST)
        if name == "upload-begin":
            values = self.single(params, "path", "name", "replace")
            ok = values is not None and values[2] in ("0", "1")
            result = up.begin(values[0], values[1], values[2] == "1") if ok else bad
        elif name == "upload-data":
            values = self.single(params, "id", "offset")
            ok = values is not None and all(HEX_OFFSET.match(v) for v in values) and body is not None
            result = up.data(int(values[0], 16), int(values[1], 16), body, chunk_max) if ok else bad
        elif name == "upload-finish":
            values = self.single(params, "id", "size", "crc")
            ok = values is not None and HEX_OFFSET.match(values[0]) and HEX_OFFSET.match(values[1]) \
                and HEX_PARAMETER.match(values[2])
            result = up.finish(int(values[0], 16), int(values[1], 16), int(values[2], 16)) if ok else bad
        elif name == "upload-abort":
            values = self.single(params, "id")
            ok = values is not None and HEX_OFFSET.match(values[0])
            result = up.abort(int(values[0], 16)) if ok else bad
        else:
            self.reply(404, b"NOT FOUND")
            return
        self.reply(200, result, "application/octet-stream")

    def chat_request(self, name, query):
        params = self.query_params(query)
        c = self.server.chat
        bad = chat.reply(files.STATUS_BAD_REQUEST)
        if name == "new":                                      # optional: width=<hex> (XT)
            width = self.optional_width(params)
            result = c.new(width) if width else bad
        elif name == "ask":
            values = self.single(params, "id", "text")
            result = c.ask(int(values[0], 16), values[1]) if values and HEX_OFFSET.match(values[0]) else bad
        elif name == "read":
            values = self.single(params, "id", "line", "count")
            if values and HEX_OFFSET.match(values[0]) and HEX_PARAMETER.match(values[1]) \
                    and HEX_PARAMETER.match(values[2]):
                result = c.read(int(values[0], 16), int(values[1], 16), int(values[2], 16))
            else:
                result = chat.READ_HEADER.pack(files.PROTOCOL_VERSION, files.STATUS_BAD_REQUEST, 0, 0, 0, 1)
        elif name == "stop":
            values = self.single(params, "id")
            result = c.stop(int(values[0], 16)) if values and HEX_OFFSET.match(values[0]) else bad
        else:
            self.reply(404, b"NOT FOUND")
            return
        self.reply(200, result, "application/octet-stream")

    def ai_request(self, name, query, body=None):
        params = self.query_params(query)
        w = self.server.writer
        bad = writer.reply(files.STATUS_BAD_REQUEST)
        if name == "begin":                                    # optional (XT): big=1, a 60 KB text, 4096-byte pieces
            big = self.optional_big(params)
            if big is not None and params is not None:
                params = {k: v for k, v in params.items() if k != "big"}
            values = self.single(params, "mode", "ilen")
            ok = big is not None and values is not None and values[0] in ("1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16", "17", "18") \
                and HEX_PARAMETER.match(values[1])
            result = w.begin(int(values[0]), int(values[1], 16), big) if ok else bad
        elif name == "data":
            values = self.single(params, "id", "offset")
            ok = values is not None and all(HEX_OFFSET.match(v) for v in values) and body is not None
            result = w.data(int(values[0], 16), int(values[1], 16), body) if ok else bad
        elif name == "go":
            values = self.single(params, "id", "size", "crc")
            ok = values is not None and HEX_OFFSET.match(values[0]) and HEX_OFFSET.match(values[1]) \
                and HEX_PARAMETER.match(values[2])
            result = w.go(int(values[0], 16), int(values[1], 16), int(values[2], 16)) if ok else bad
        elif name == "read":
            values = self.single(params, "id", "offset", "count")
            if values and all(HEX_OFFSET.match(v) for v in values[:2]) and HEX_PARAMETER.match(values[2]):
                result = w.read(int(values[0], 16), int(values[1], 16), int(values[2], 16))
            else:
                result = writer.READ_HEADER.pack(files.PROTOCOL_VERSION, files.STATUS_BAD_REQUEST, 1, 0, 0, 0, 0)
        elif name == "stop":
            values = self.single(params, "id")
            result = w.stop(int(values[0], 16)) if values and HEX_OFFSET.match(values[0]) else bad
        else:
            self.reply(404, b"NOT FOUND")
            return
        self.reply(200, result, "application/octet-stream")

    def change_request(self, name, query):
        params = self.query_params(query)
        if name == "rename":
            values = self.single(params, "path", "name", "new")
            result = changes.rename(self.server.root, *values, log=log) if values else None
        else:
            values = self.single(params, "path", "name")
            result = changes.delete(self.server.root, *values, log=log) if values else None
        self.reply(200, result or changes.reply(files.STATUS_BAD_REQUEST), "application/octet-stream")

    def do_POST(self):
        path, _, query = self.path.partition("?")
        length = self.headers.get("Content-Length", "")
        post_paths = (FILES_PREFIX + "upload-data", AI_PREFIX + "data")
        raw = self.headers.get_content_type() == RAW_CONTENT_TYPE      # the XT: the chunk itself
        limit = uploads.RAW_CHUNK_MAX if raw else uploads.POST_BODY_MAX
        if path not in post_paths or not length.isdigit() or int(length) > limit:
            self.close_connection = True
            self.reply(200 if path in post_paths else 404,
                       uploads.reply(files.STATUS_BAD_REQUEST), "application/octet-stream")
            return
        body = self.rfile.read(int(length))
        if raw:
            body = uploads.wrap_post_body(body)
        if path == AI_PREFIX + "data":
            self.ai_request("data", query, body)
        else:
            self.upload_request("upload-data", query, body, uploads.RAW_CHUNK_MAX if raw else uploads.UPLOAD_CHUNK_MAX)

    def files_request(self, name, query):
        """list?path=&start=&count=, text?path=&line=&count=, info?path=,
        read?path=&offset=&count= (numbers in hex)."""
        try:
            params = parse_qs(query, keep_blank_values=True, strict_parsing=bool(query),
                              encoding="ascii", errors="strict")
        except (ValueError, UnicodeDecodeError):
            params = None
        if name == "info":
            path = params["path"][0] if params and len(params.get("path", [])) == 1 and len(params) == 1 else None
            body = files.info_response(self.server.root, path) if path is not None else \
                files.INFO_REPLY.pack(files.PROTOCOL_VERSION, files.STATUS_BAD_REQUEST, 0, 0, 0)
            self.reply(200, body, "application/octet-stream")
            return
        if name in ("doc-info", "doc-read"):              # optional (XT): big=1, a 60 KB document, 4096-byte pieces
            big = self.optional_big(params)
            marks = self.optional_marks(params)
            if big is None or marks is None:
                self.reply(200, files.INFO_REPLY.pack(files.PROTOCOL_VERSION, files.STATUS_BAD_REQUEST, 0, 0, 0)
                           if name == "doc-info" else
                           files.READ_HEADER.pack(files.PROTOCOL_VERSION, files.STATUS_BAD_REQUEST, 0, 0, 0),
                           "application/octet-stream")
                return
            params = {k: v for k, v in params.items() if k not in ("big", "marks")} if params is not None else None
        if name == "doc-info":
            values = self.single(params, "path", "name")
            body = files.doc_info_response(self.server.root, *values, big=big, marks=marks) if values else \
                files.INFO_REPLY.pack(files.PROTOCOL_VERSION, files.STATUS_BAD_REQUEST, 0, 0, 0)
            self.reply(200, body, "application/octet-stream")
            return
        if name == "doc-read":
            values = self.single(params, "path", "name", "offset", "count")
            if values and HEX_OFFSET.match(values[2]) and HEX_PARAMETER.match(values[3]):
                body = files.doc_read_response(self.server.root, values[0], values[1],
                                               int(values[2], 16), int(values[3], 16), big=big, marks=marks)
            else:
                body = files.READ_HEADER.pack(files.PROTOCOL_VERSION, files.STATUS_BAD_REQUEST, 0, 0, 0)
            self.reply(200, body, "application/octet-stream")
            return
        if name == "read":
            values = None
            big = False
            if params is not None and all(len(params.get(k, [])) == 1 for k in ("path", "offset", "count")):
                offset, count = params["offset"][0], params["count"][0]
                if HEX_OFFSET.match(offset) and HEX_PARAMETER.match(count):
                    values = (params["path"][0], int(offset, 16), int(count, 16))
                if "big" in params:                       # optional (XT): a larger piece, up to 4096 bytes
                    big = params["big"] == ["1"]
                    if not big:
                        values = None                      # big must be exactly 1
            body = files.read_response(self.server.root, *(values or ("", 0, 0)), big=big)
            self.reply(200, body, "application/octet-stream")
            return
        if name == "list":
            first_key, build = "start", files.list_response
        elif name == "text":
            first_key, build = "line", files.text_response
        else:
            self.reply(404, b"NOT FOUND")
            return
        values = None
        extra = ()
        big = False
        if params is not None and "big" in params:        # optional (NetFiles/VGA): up to 32 entries or lines, big must be exactly 1
            big = params["big"] == ["1"]
            if not big:
                params = None
        if params is not None and all(len(params.get(k, [])) == 1 for k in ("path", first_key, "count")):
            first, count = params[first_key][0], params["count"][0]
            if HEX_PARAMETER.match(first) and HEX_PARAMETER.match(count):
                values = (params["path"][0], int(first, 16), int(count, 16))
            if name == "text" and "width" in params:      # optional (XT): 20-80, default 40
                width = self.single({"width": params["width"]}, "width")
                ok = width is not None and HEX_PARAMETER.match(width[0])
                extra = (int(width[0], 16),) if ok else (0,)
        if values is None:
            values = ("", 0, 0)             # count 0 is always a bad request
        body = build(self.server.root, *values, *extra, big=big) if big else build(self.server.root, *values, *extra)
        self.reply(200, body, "application/octet-stream")

    def reply(self, status, body, content_type="text/plain; charset=us-ascii"):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_request(self, code="-", size="-"):
        log.info("%s %s %s -> %s (User-Agent: %s)", self.client_address[0],
                 self.command, self.path, code, self.headers.get("User-Agent", "-"))

    def log_message(self, fmt, *args):
        log.warning("%s: %s", self.client_address[0], fmt % args)


def make_server(host="0.0.0.0", port=DEFAULT_PORT, slow_seconds=20.0, root=DEFAULT_ROOT,
                ollama_url=chat.DEFAULT_OLLAMA_URL, model=chat.DEFAULT_MODEL):
    root = Path(root).expanduser().resolve()
    if not root.is_dir():
        raise SystemExit("storage root %s is not a directory" % root)
    server = ThreadingHTTPServer((host, port), Handler)
    server.daemon_threads = True
    server.slow_seconds = slow_seconds
    server.root = root
    removed = uploads.remove_leftovers(root)
    if removed:
        log.info("removed %d unfinished upload file(s) from an earlier run", removed)
    server.uploads = uploads.Uploads(root, log)
    server.chat = chat.Chat(ollama_url, model, log)
    server.writer = writer.Writer(server.chat, log, root)
    return server


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--bind", default="0.0.0.0", help="address to listen on (default 0.0.0.0)")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT, help="TCP port (default %d)" % DEFAULT_PORT)
    parser.add_argument("--root", default=str(DEFAULT_ROOT),
                        help="directory the C64 may browse (default host/sample_root)")
    parser.add_argument("--model", default=chat.DEFAULT_MODEL,
                        help="Ollama model for the chat (default %s)" % chat.DEFAULT_MODEL)
    parser.add_argument("--ollama-url", default=chat.DEFAULT_OLLAMA_URL,
                        help="Ollama API address (default %s)" % chat.DEFAULT_OLLAMA_URL)
    parser.add_argument("--slow-seconds", type=float, default=20.0,
                        help="delay of the /slow endpoint; must exceed the WiC64 remote timeout (5 s)")
    args = parser.parse_args(argv)
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(message)s", stream=sys.stdout)
    server = make_server(args.bind, args.port, args.slow_seconds, args.root, args.ollama_url, args.model)
    log.info("C64 Workstation host, protocol %d, listening on %s:%d, storage root %s, chat model %s",
             PROTOCOL_VERSION, args.bind, args.port, server.root, args.model)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
