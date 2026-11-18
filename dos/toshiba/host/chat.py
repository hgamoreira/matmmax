"""Chat with a local LLM for the C64 (protocol v1, /c64/v1/chat-*; 04_reader 04A).

The model runs in Ollama on this machine (its local HTTP API; standard
library only). The C64 never waits for the model: chat-ask starts the answer
in a background thread, and chat-read returns the conversation as
ready-to-display 40-column lines, however far it has got.

Lines never change once a later line exists (greedy word wrap), so the C64
only re-reads its last line while an answer grows.

The model's output is only text for the C64. Nothing it writes is executed.
"""
import json
import re
import secrets
import struct
import threading
import time
import urllib.error
import urllib.request

import files

DEFAULT_OLLAMA_URL = "http://127.0.0.1:11434"
DEFAULT_MODEL = "qwen3:14b"                 # the user's choice (2026-09-25)
SYSTEM_PROMPT = (
    "You are chatting with someone using a Commodore 64 with a 40-column text screen. "
    "Answer in plain text: no Markdown, no tables, no emoji. Keep answers short and "
    "clear unless asked for more detail. Use simple lists with '-' when useful.")
SYSTEM_PROMPT_WIDE = (
    "You are chatting with someone using an old IBM PC with a %d-column text screen. "
    "Answer in plain text: no Markdown, no tables, no emoji. Keep answers short and "
    "clear unless asked for more detail. Use simple lists with '-' when useful.")


def system_prompt(width):
    return SYSTEM_PROMPT if width == files.TEXT_WIDTH else SYSTEM_PROMPT_WIDE % width

QUESTION_MAX = 240                          # characters from the C64
READ_COUNT_MAX = 20                         # lines per chat-read
MODEL_NAME_MAX = 32
SESSIONS_MAX = 4
SESSION_IDLE_SECONDS = 60 * 60
HISTORY_MESSAGES_MAX = 20                   # sent to the model as context
ANSWER_CHARS_MAX = 16000                    # longer answers are cut off
CONNECT_TIMEOUT = 5
READ_TIMEOUT = 180                          # between streamed chunks

# Line attributes (how the C64 shows a line)
LINE_ANSWER, LINE_QUESTION, LINE_INFO = 0, 1, 2

STATUS_UNKNOWN_SESSION = 14
STATUS_BUSY = 15                            # an answer is still being written
STATUS_LLM_UNAVAILABLE = 16

SESSION_REPLY = struct.Struct("<BBI")                 # version, status, session id
READ_HEADER = struct.Struct("<BBHHBB")                # version, status, total, first, count, done


def reply(status, session_id=0):
    return SESSION_REPLY.pack(files.PROTOCOL_VERSION, status, session_id & 0xFFFFFFFF)


def new_reply(status, session_id=0, model=""):
    name = model.encode("ascii", "replace")[:MODEL_NAME_MAX]
    return reply(status, session_id) + bytes([len(name)]) + name


def read_response_max(width=files.TEXT_WIDTH):
    return READ_HEADER.size + READ_COUNT_MAX * (2 + width)


_MARKDOWN = [
    (re.compile(r"<think>.*?(</think>|$)", re.S), ""),       # in case a model still thinks aloud
    (re.compile(r"^```.*$", re.M), ""),                      # code fences
    (re.compile(r"^\s{0,3}#{1,6}\s*", re.M), ""),            # headings
    (re.compile(r"\*\*(.+?)\*\*|__(.+?)__", re.S), lambda m: m.group(1) or m.group(2)),
    (re.compile(r"`([^`]*)`"), r"\1"),
    (re.compile(r"^(\s*)[*+]\s+", re.M), r"\1- "),           # bullets
    (re.compile(r"\n{3,}"), "\n\n"),
]


def plain_text(text):
    for pattern, replacement in _MARKDOWN:
        text = pattern.sub(replacement, text)
    return text.strip("\n")


def wrap(text, prefix="", width=files.TEXT_WIDTH):
    """Rows of at most `width` columns (40 by default); with a prefix ("> "), later rows are indented under it.
    Trailing spaces are dropped (they only cost bytes on the WiC64 link)."""
    rows = []
    indent = " " * len(prefix)
    for index, line in enumerate(text.split("\n")):
        wrapped = files.wrap_line(files.to_c64_ascii(line), width - len(prefix))
        for number, row in enumerate(wrapped):
            lead = prefix if index == 0 and number == 0 else indent
            rows.append((lead + row).rstrip())
    return rows


class Session:
    def __init__(self, session_id, width=files.TEXT_WIDTH):
        self.id = session_id
        self.width = width
        self.turns = []                 # [question, answer text, info or None]
        self.messages = []              # sent to the model: user/assistant
        self.generating = False
        self.stop = threading.Event()
        self.touched = time.monotonic()

    def lines(self):
        """The whole conversation as (attribute, text) lines."""
        out = []
        for question, answer, info in self.turns:
            if out:
                out.append((LINE_ANSWER, ""))
            out.extend((LINE_QUESTION, row) for row in wrap(question, "> ", self.width))
            text = plain_text(answer)
            if text:
                out.extend((LINE_ANSWER, row) for row in wrap(text, "", self.width))
            if info:
                out.extend((LINE_INFO, row) for row in wrap(info, "", self.width))
        return out


class Chat:
    def __init__(self, ollama_url=DEFAULT_OLLAMA_URL, model=DEFAULT_MODEL, log=None):
        self.ollama_url = ollama_url.rstrip("/")
        self.model = model
        self.log = log
        self.sessions = {}
        self.lock = threading.Lock()

    def note(self, text, *args):
        if self.log:
            self.log.info("CHAT " + text, *args)

    def expire(self):
        now = time.monotonic()
        for session_id, session in list(self.sessions.items()):
            if not session.generating and now - session.touched > SESSION_IDLE_SECONDS:
                del self.sessions[session_id]

    def model_available(self):
        return model_available(self.ollama_url, self.model)

    # --- endpoints ---
    def new(self, width=files.TEXT_WIDTH):
        if not files.valid_width(width):
            return new_reply(files.STATUS_BAD_REQUEST, 0, self.model)
        if not self.model_available():
            return new_reply(STATUS_LLM_UNAVAILABLE, 0, self.model)
        with self.lock:
            self.expire()
            if len(self.sessions) >= SESSIONS_MAX:
                oldest = min((s for s in self.sessions.values() if not s.generating),
                             key=lambda s: s.touched, default=None)
                if oldest is None:
                    return new_reply(STATUS_BUSY, 0, self.model)
                del self.sessions[oldest.id]
            session_id = 0
            while session_id == 0 or session_id in self.sessions:
                session_id = secrets.randbits(32)
            self.sessions[session_id] = Session(session_id, width)
        self.note("%08x new conversation (%s)", session_id, self.model)
        return new_reply(files.STATUS_OK, session_id, self.model)

    def ask(self, session_id, question):
        if not question or len(question) > QUESTION_MAX or \
                not all(0x20 <= ord(c) <= 0x7E for c in question):
            return reply(files.STATUS_BAD_REQUEST, session_id)
        with self.lock:
            session = self.sessions.get(session_id)
            if session is None:
                return reply(STATUS_UNKNOWN_SESSION, session_id)
            if session.generating:
                return reply(STATUS_BUSY, session_id)
            session.generating = True
            session.stop.clear()
            session.touched = time.monotonic()
            turn = [question, "", "(thinking...)"]
            session.turns.append(turn)
            session.messages.append({"role": "user", "content": question})
        threading.Thread(target=self.answer, args=(session, turn), daemon=True).start()
        self.note("%08x asks: %s", session_id, question)
        return reply(files.STATUS_OK, session_id)

    def read(self, session_id, first, count):
        if count < 1 or count > READ_COUNT_MAX:
            return READ_HEADER.pack(files.PROTOCOL_VERSION, files.STATUS_BAD_REQUEST, 0, first & 0xFFFF, 0, 0)
        with self.lock:
            session = self.sessions.get(session_id)
            if session is None:
                return READ_HEADER.pack(files.PROTOCOL_VERSION, STATUS_UNKNOWN_SESSION, 0, first & 0xFFFF, 0, 1)
            session.touched = time.monotonic()
            lines = session.lines()
            done = 0 if session.generating else 1
        total = min(len(lines), 0xFFFF)
        page = lines[first:first + count] if first < total else []
        body = bytearray(READ_HEADER.pack(files.PROTOCOL_VERSION, files.STATUS_OK, total, first, len(page), done))
        for attribute, text in page:
            encoded = text.encode("ascii")
            body += bytes([attribute, len(encoded)]) + encoded
        return bytes(body)

    def stop(self, session_id):
        with self.lock:
            session = self.sessions.get(session_id)
            if session is None:
                return reply(STATUS_UNKNOWN_SESSION, session_id)
            session.stop.set()
        return reply(files.STATUS_OK, session_id)

    # --- the model, in a background thread ---
    def answer(self, session, turn):
        messages = [{"role": "system", "content": system_prompt(session.width)}] + session.messages[-HISTORY_MESSAGES_MAX:]

        def add(text):
            with self.lock:
                turn[1] += text
                turn[2] = None
                if len(turn[1]) > ANSWER_CHARS_MAX:
                    return "(answer cut off)"
            return None

        info = stream(self.ollama_url, self.model, messages, session.stop, add)
        with self.lock:
            turn[2] = info
            if turn[1]:
                session.messages.append({"role": "assistant", "content": turn[1]})
            else:
                session.messages.pop()          # keep user/assistant pairs
            session.generating = False
            session.touched = time.monotonic()
        self.note("%08x answered (%d characters)%s", session.id, len(turn[1]), " " + info if info else "")


def installed_models(ollama_url):
    """The names of the models Ollama has, sorted, or None if it does not answer."""
    try:
        with urllib.request.urlopen(ollama_url + "/api/tags", timeout=CONNECT_TIMEOUT) as r:
            return sorted(m.get("name") for m in json.load(r).get("models", []) if m.get("name"))
    except (OSError, ValueError):
        return None


def model_available(ollama_url, model):
    try:
        with urllib.request.urlopen(ollama_url + "/api/tags", timeout=CONNECT_TIMEOUT) as r:
            names = [m.get("name") for m in json.load(r).get("models", [])]
    except (OSError, ValueError):
        return False
    return model in names or model + ":latest" in names


def stream(ollama_url, model, messages, stop, add, options=None):
    """Stream the model's answer to add(text) (also used by writer.py).

    add may return an info text to end the answer early. Returns None when
    the answer is complete, otherwise "(stopped)", "(error: ...)" or add's text.
    """
    request_body = {"model": model, "stream": True, "think": False, "messages": messages}
    if options:
        request_body["options"] = options
    request = urllib.request.Request(ollama_url + "/api/chat", data=json.dumps(request_body).encode(),
                                     headers={"Content-Type": "application/json"})
    try:
        # The timeout covers each wait for data, so it must allow for the
        # model's longest pause between chunks (loading it takes seconds).
        with urllib.request.urlopen(request, timeout=READ_TIMEOUT) as response:
            for raw in response:
                if stop.is_set():
                    return "(stopped)"
                chunk = json.loads(raw)
                if "error" in chunk:
                    return "(error: %s)" % chunk["error"]
                info = add(chunk.get("message", {}).get("content", ""))
                if info:
                    return info
                if chunk.get("done"):
                    break
    except urllib.error.HTTPError as e:
        try:
            detail = json.load(e).get("error", str(e.code))
        except ValueError:
            detail = str(e.code)
        return "(error: %s)" % detail
    except (OSError, ValueError) as e:
        return "(error: the model does not answer: %s)" % (getattr(e, "reason", None) or e)
    return None
