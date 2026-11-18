"""AI writing help for WordSmith/64 (protocol v1, /c64/v1/ai-*; milestone 03B).

The C64 sends an instruction and some of its text (the same chunked,
checksummed way as an upload, but kept in memory, never written to disk),
then polls for the answer, which it inserts into its document as it arrives.
The model is the chat's (chat.py): Ollama on this machine.

Modes (the user's choice, 2026-09-25):
  1 write     the instruction alone; the answer is new text for the cursor
  2 ask       the whole document and a question about it
  3 rewrite   one paragraph and how to rewrite it (empty: fix mistakes)
  4 continue  the text before the cursor; the answer continues it
Code modes for CodeSmith (the PS/1's C editor, Turbo C 2.01; XD42; additive - WordSmith's modes are unchanged):
  5 explain   the build's message (instruction) and the code around it (numbered lines); the answer is a C comment
  6 fix       the message (may be empty) and the function; the answer is the corrected function
  7 write     what to write (instruction) and the file's #include lines (context, may be empty); the answer is code
Project modes (XD44): the same three jobs, but the model reads the whole program that CodeSmith mirrored on this PC in
code/<project>/ (the .C/.H or .PAS/.INC files), so the PS/1 sends only a short descriptor instead of the code:
  8 explain   9 fix   10 write     instruction as in 5-7; context: "project=NAME", "file=MAIN.C", "line=N" (the message's line
  or the cursor's), and for fix "from=A", "to=B" (the lines of the function to rewrite), one per line.
A code answer is kept as the model wrote it (only code fences and <think> go), so that ** or * in C survive.

The answer is plain ASCII with LF between paragraphs, never re-wrapped
(WordSmith wraps). Only ever appended to: whatever the C64 has read stays
valid, so Markdown is removed line by line and a line is only released up to
a point its cleaning can no longer change.

The model's output is only text for the C64. Nothing it writes is executed.
"""
import os
import re
import secrets
import struct
import threading
import time

import chat
import files
import mathsmith_lint
import uploads

MODE_WRITE, MODE_ASK, MODE_REWRITE, MODE_CONTINUE = 1, 2, 3, 4
MODE_CODE_EXPLAIN, MODE_CODE_FIX, MODE_CODE_WRITE = 5, 6, 7
MODE_PROJ_EXPLAIN, MODE_PROJ_FIX, MODE_PROJ_WRITE = 8, 9, 10
MODE_CODE_CHANGE, MODE_PROJ_CHANGE = 11, 12      # the user's instruction changes the function at the cursor (XD45)
MODE_MATH_ASK, MODE_MATH_CODE, MODE_MATH_EXPLAIN, MODE_MATH_FIX, MODE_MATH_WHY = 13, 14, 15, 16, 17     # MathSmith (XD75)
MODE_MATH_CHANGE = 18                                  # MathSmith's editor (XD78): change a script as the user asks
MATH_MODES = (MODE_MATH_ASK, MODE_MATH_CODE, MODE_MATH_EXPLAIN, MODE_MATH_FIX, MODE_MATH_WHY, MODE_MATH_CHANGE)
MATH_CODE_MODES = (MODE_MATH_CODE, MODE_MATH_FIX, MODE_MATH_CHANGE)      # their answers are code, kept as written
PROJECT_MODES = (MODE_PROJ_EXPLAIN, MODE_PROJ_FIX, MODE_PROJ_WRITE, MODE_PROJ_CHANGE)
CODE_MODES = (MODE_CODE_EXPLAIN, MODE_CODE_FIX, MODE_CODE_WRITE, MODE_CODE_CHANGE) + PROJECT_MODES
PROJECT_CHARS_MAX = 24000                   # the project text given to the model (the other files, then the current one)
CURRENT_CHARS_MAX = 14000                   # ... of which the current file at most (a window around the line if longer)
WINDOW_LINES = 120
COMMENT_WIDTH = 72                          # an explanation's lines, inside its /* */
INSTRUCTION_MAX = 240
CONTEXT_MAX = 32768                         # WordSmith's whole text buffer
CONTEXT_BIG_MAX = 61440                     # ... WordSmith/XT's, when ai-begin says big=1
READ_COUNT_MAX = 512                        # the C64's chunk size
JOBS_MAX = 4
JOB_IDLE_SECONDS = 10 * 60
ANSWER_CHARS_MAX = 16000
CONTEXT_WINDOW_DEFAULT = 4096               # Ollama's usual num_ctx
CONTEXT_WINDOW_MAX = 32768                  # qwen3 handles 32K tokens
ANSWER_TOKENS_ALLOWANCE = 2048

STATUS_OUT_OF_ORDER = uploads.STATUS_OUT_OF_ORDER       # 9
STATUS_CHECK_FAILED = uploads.STATUS_CHECK_FAILED       # 10
STATUS_UNKNOWN_JOB = chat.STATUS_UNKNOWN_SESSION        # 14
STATUS_BUSY = chat.STATUS_BUSY                          # 15
STATUS_LLM_UNAVAILABLE = chat.STATUS_LLM_UNAVAILABLE    # 16
STATUS_LLM_FAILED = 17                      # the model failed before writing anything

JOB_REPLY = uploads.UPLOAD_REPLY            # version, status, job id, bytes received
READ_HEADER = struct.Struct("<BBBHHHH")     # version, status, done, total, offset, count, crc

SYSTEM_PROMPT = (
    "You are the writing assistant of a word processor on a Commodore 64. "
    "Whatever you write is inserted directly into the user's document. So write only that "
    "text: no introduction, no comments about what you did, no quotation marks around it. "
    "Plain text only: no Markdown, no tables, no emoji. Separate paragraphs with a blank line. "
    "Unless told otherwise, write in the language of the user's text.")
DEFAULT_REWRITE = "Correct the spelling, grammar and punctuation. Keep the meaning, tone and language."
CODE_SYSTEM_PROMPT = (
    "You are the programming assistant of a C editor on an IBM PS/1 (an Intel 486) under MS-DOS 6.22. "
    "Programs are compiled with Turbo C 2.01 from 1989: C89 only. Use only /* */ comments, never //. "
    "Declare variables at the start of a block. int is 16 bits and long is 32 bits. "
    "Use only Turbo C's own library (stdio.h, stdlib.h, string.h, ctype.h, math.h, conio.h, dos.h, time.h, graphics.h). "
    "No C99 or C++ features. Whatever you write goes straight into the user's source file.")
PASCAL_SYSTEM_PROMPT = (
    "You are the programming assistant of a Pascal editor on an IBM PS/1 (an Intel 486) under MS-DOS 6.22. "
    "Programs are compiled with Turbo Pascal 7.0 from 1992. Use { } or (* *) comments. "
    "Integer is 16 bits and LongInt is 32 bits. Use only Turbo Pascal's own units (System, Crt, Dos, Graph, Strings, Printer). "
    "No Delphi or Free Pascal features. Whatever you write goes straight into the user's source file.")
MATH_GUIDE_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "mathsmith_guide.txt")
MATH_INTRO = ("You are the assistant of MathSmith, a small MATLAB-like calculator and programming language for MS-DOS. "
              "Answer in plain ASCII, no Markdown, short: the screen has 80 columns.")
MATH_PLATFORMS = {                               # what the machine behind the request can do (XD87); "" is the Toshiba T2100
    "": ("This copy runs on a Toshiba T2100, an 8086 with no maths chip and an 80-column, 25-row monochrome text screen; "
         "plots are 640x200 in black and white. It is slow: keep programs and sizes small and prefer vectorised code. "
         "Limits: a matrix of at most 64 by 64 (4096 elements), 40 variables, 12 user functions, 3000 statements in a script."),
    "486": ("This copy runs on an IBM PS/1 with a 486 and a maths chip, so it is fast, with an 80-column, 50-row colour text screen; "
            "plots are 640x480 with 16 colours, and the colour letters of a style such as 'r--' show. "
            "Limits: a matrix of at most 250000 elements (500 by 500), 200 variables, 60 user functions, 20000 statements in a script."),
}
MATH_HAS_FFT = ("486", "")                      # the machines whose MathSmith has fft, ifft, cabs and cangle (XD101; "" is the Toshiba and the XT, XD102)
MATH_FFT_POW2 = ("",)                           # ... and of those, the ones that do only powers of two
MATH_HEADER_LINE = re.compile(r"\A#(?:(machine|model|list)=([!-~]{1,64})|(sizes)=([!-~]{1,600}))\n")
_math_guide_cache = {}


def math_guide():
    """(the language guide, its worked examples, its checklist) from mathsmith_guide.txt: the checklist is the '%% CHECKLIST' block, the examples what
    follows the first '%% FILE' line."""
    if "text" not in _math_guide_cache:
        with open(MATH_GUIDE_FILE, encoding="ascii") as f:
            text = f.read().strip("\n")
        check = re.search(r"^%% CHECKLIST\n(.*?)^%% END\n*", text, re.M | re.S)
        checklist = check.group(1).strip("\n") if check else ""
        if check:
            text = text[:check.start()] + text[check.end():]
        for tag in ("FFT", "FFT16"):                    # the Fourier functions of the builds that have them (XD100-XD102), and the 16-bit machines' length rule: not part of the language text
            block = re.search(r"^%% " + tag + r"\n(.*?)^%% END\n*", text, re.M | re.S)
            _math_guide_cache[tag.lower()] = block.group(1).strip("\n") if block else ""
            if block:
                text = text[:block.start()] + text[block.end():]
        at = text.find("%% FILE")
        _math_guide_cache["text"] = (text.rstrip("\n"), "", checklist) if at < 0 else (text[:at].rstrip("\n"), text[at:], checklist)
    return _math_guide_cache["text"]


def math_fft_note(machine="486"):
    """The paragraph that tells the model about fft, ifft, cabs and cangle, for a machine that has them (MATH_HAS_FFT): the Linux build and the 486 take any length; the
    16-bit machines (machine "": the Toshiba, the XT) only powers of two, which a second sentence says."""
    math_guide()
    return _math_guide_cache["fft"] + ((" " + _math_guide_cache["fft16"]) if machine in MATH_FFT_POW2 else "")


def math_missing():
    """The names that MATLAB has and MathSmith has not, from the guide's 'There is no ...' sentences of its function list."""
    if "missing" not in _math_guide_cache:
        language = math_guide()[0]
        section = language[language.index("BUILT-IN FUNCTIONS"):language.index("ERROR MESSAGES")] if "BUILT-IN FUNCTIONS" in language else language
        names = []
        for sentence in re.findall(r"There is no ([^.]*)\.", section):
            names += [w for w in re.split(r"[ ,]+", sentence) if w and w not in ("and", "or")]
        _math_guide_cache["missing"] = tuple(dict.fromkeys(names))
    return _math_guide_cache["missing"]


def math_review(script):
    """What goes into a prompt that writes or corrects code (XD87): the checker's certain findings for the script, if any, then the checklist."""
    text = ""
    found = mathsmith_lint.check(script, math_missing()) if script else []
    if found:
        text += ("A checker has found these problems in the script. They are certain: correct every one of them, besides what else you find.\n"
                 + "\n".join("  line %d: %s" % f for f in found) + "\n\n")
    return text + math_guide()[2] + "\n"


def split_headers(context, allowed=None):
    """({name: value}, the rest) for the '#name=value' lines a MathSmith request may start with (XD87, XD89): machine (which program asks), model (the
    Ollama model the user chose in the program) and list (models: the answer is the list of the installed models, no model is asked). Unknown names
    are not headers: they stay in the text."""
    headers = {}
    while True:
        m = MATH_HEADER_LINE.match(context)
        if m is None or (allowed is not None and (m.group(1) or m.group(3)) not in allowed):
            return headers, context
        headers.setdefault(m.group(1) or m.group(3), m.group(2) or m.group(4))
        context = context[m.end():]


def split_machine(context):
    """('486', the rest) when the context starts with the '#machine=486' line that the 486 build sends (XD87), else ('', context)."""
    headers, rest = split_headers(context)
    machine = headers.get("machine", "")
    return (machine if machine in MATH_PLATFORMS else ""), rest


def math_system_prompt(machine="", examples=True):
    """MathSmith's system prompt: what the assistant is, the machine it is on, the whole language, and (for the modes that write code) examples."""
    language, shown, _ = math_guide()
    note = MATH_PLATFORMS.get(machine, MATH_PLATFORMS[""])
    if machine in MATH_HAS_FFT:
        note += " " + math_fft_note(machine)
    return "\n\n".join([MATH_INTRO, note, language] + ([shown] if examples and shown else []))


MATH_CODE_ONLY = ("Reply with the code only, ready to save as a .m file and run: no explanation, no Markdown, no code fences. "
                  "Comments with % only. Use only what the language above has.")
CODE_ONLY = ("Reply with the code only, ready to go into the file: no explanation, no Markdown, no code fences, "
             "/* */ comments only.")
PASCAL_CODE_ONLY = ("Reply with the code only, ready to go into the file: no explanation, no Markdown, no code fences, "
                    "{ } comments only (no //), and do not repeat the program line or the uses clause.")


def reply(status, job_id=0, received=0):
    return JOB_REPLY.pack(files.PROTOCOL_VERSION, status, job_id & 0xFFFFFFFF, received & 0xFFFFFFFF)


def read_response_max():
    return READ_HEADER.size + READ_COUNT_MAX


def parse_descriptor(context):
    """{'project': 'GAME', 'file': 'MAIN.C', 'line': 6, ...} from the lines of a project job's context, or None."""
    values = {}
    for line in context.split("\n"):
        if "=" in line:
            key, _, value = line.partition("=")
            values[key.strip().lower()] = value.strip()
    try:
        d = {"project": values["project"], "file": values["file"], "line": int(values.get("line", "0")),
             "from": int(values.get("from", "0")), "to": int(values.get("to", "0"))}
    except (KeyError, ValueError):
        return None
    if not files.is_c64_name(d["project"]) or not files.is_c64_name(d["file"]) or min(d["line"], d["from"], d["to"]) < 0:
        return None
    return d


def language_of(name):
    return files.PROJECT_EXTENSIONS.get(("." + name.rsplit(".", 1)[-1]).lower(), "c")


def numbered(text, first=1):
    return "".join("%d: %s\n" % (first + i, line) for i, line in enumerate(text.split("\n")))


def current_file_view(text, line):
    """The current file with numbered lines; if it is long, a window around `line`."""
    lines = text.split("\n")
    if len(text) <= CURRENT_CHARS_MAX:
        return numbered(text), ""
    first = max(1, line - WINDOW_LINES)
    last = min(len(lines), line + WINDOW_LINES)
    return numbered("\n".join(lines[first - 1:last]), first), \
        "(Only lines %d-%d of the %d are shown.)\n" % (first, last, len(lines))


def build_project_request(mode, instruction, d, project_files):
    """The user message of a project job: the list of the project's files, the other files, the current file with numbered
    lines, and the task. `project_files` is [(name, text)]; d is parse_descriptor's result."""
    pascal = language_of(d["file"]) == "pascal"
    language = "Turbo Pascal 7.0" if pascal else "Turbo C 2.01"
    code_only = PASCAL_CODE_ONLY if pascal else CODE_ONLY
    part = "routine (or main block with its var, const and type sections)" if pascal else "function"
    names = ["%s (%.1f KB)" % (n, len(t) / 1024.0) for n, t in project_files]
    current = next((t for n, t in project_files if n.casefold() == d["file"].casefold()), "")
    out = ['The program "%s" (%s) is on the PC in these files: %s.\n' % (d["project"], language, ", ".join(names) or "none")]
    budget = PROJECT_CHARS_MAX - min(len(current), CURRENT_CHARS_MAX)
    shown, skipped = [], []
    for n, t in project_files:
        if n.casefold() == d["file"].casefold():
            continue
        if len(t) <= budget:
            shown.append("----- file %s -----\n%s\n" % (n, t))
            budget -= len(t)
        else:
            skipped.append(n)
    out.extend(shown)
    if skipped:
        out.append("(Not shown, too long for the space left: %s.)\n" % ", ".join(skipped))
    view, note = current_file_view(current, d["line"] or d["from"] or 1)
    out.append("----- the file being edited: %s (each line starts with its number) -----\n%s%s-----\n"
               % (d["file"], view, note))
    if mode == MODE_PROJ_EXPLAIN:
        out.append("%s reports this message about line %d of %s:\n%s\n\nExplain in plain English, in at most 6 short lines, "
                   "what is wrong and how to fix it; you may refer to the other files. Plain text only: no code, no Markdown."
                   % (language, d["line"], d["file"], instruction))
    elif mode == MODE_PROJ_FIX:
        out.append("%s reports:\n%s\n\nRewrite the %s in lines %d-%d of %s, using the types, names and functions of the "
                   "other files where they matter. Reply with the corrected %s only (not the include lines), complete and "
                   "ready to replace the old one. %s"
                   % (language, instruction or "no message (find and fix the mistakes)", part, d["from"], d["to"], d["file"],
                      "code" if pascal else "function", code_only))
    elif mode == MODE_PROJ_CHANGE:
        out.append("Change the %s in lines %d-%d of %s as the user asks: %s\n\nReply with the new code that replaces that "
                   "%s, complete (it may add helper %s%s before it), using the types, names and functions of the "
                   "other files where they matter. %s"
                   % (part, d["from"], d["to"], d["file"], instruction, "code" if pascal else "function",
                      "routines and declarations" if pascal else "functions", "" if pascal else " and #include lines", code_only))
    else:
        out.append("Write %s code for this request: %s\nIt will be inserted into %s at line %d. Use the types, names and "
                   "functions of the project's files where they fit, and do not repeat what they already define. %s"
                   % (language, instruction, d["file"], d["line"], code_only))
    return "\n".join(out)


def build_math_request(mode, instruction, context, sizes=""):
    if mode == MODE_MATH_ASK:
        request = instruction
        if context.strip():
            request += "\n\nMy MathSmith code:\n-----\n%s\n-----" % context
        return request
    if mode == MODE_MATH_CODE:
        return "Write MathSmith code for this request: %s\n\n%s\n%s" % (instruction, math_review(""), MATH_CODE_ONLY)
    if mode == MODE_MATH_EXPLAIN:
        return ("Explain what this MathSmith script does, in at most 10 short lines.%s\n\nThe script:\n-----\n%s\n-----\n"
                "Plain text only: no Markdown." % ((" " + instruction) if instruction else "", context))
    if mode == MODE_MATH_CHANGE:
        return ("Change this MathSmith script as the user asks: %s\n\nThe script:\n-----\n%s\n-----\n"
                "%s\nReply with the complete changed script: keep what the request does not touch exactly as it is. %s"
                % (instruction, context, math_review(context), MATH_CODE_ONLY))
    if mode == MODE_MATH_FIX:
        facts = mathsmith_lint.explain_error(instruction, context)
        if sizes:
            facts = (facts + "\n" if facts else "") + "Sizes of the variables when the error happened: " + ", ".join(sizes.split(",")).replace(":", " ") + "."
        return ("Find and fix the mistakes in this MathSmith script. %s\n\nThe script:\n-----\n%s\n-----\n"
                "%s\n%sReply with the complete corrected script. Change what is really wrong, not just the line that fails: do not move the same mistake to another line. %s"
                % (instruction or "(no message: find the mistakes yourself)", context, math_review(context),
                   ("Facts about the error (certain; they matter more than the checklist):\n%s\n\n" % facts) if facts else "", MATH_CODE_ONLY))
    return ("MathSmith reports this error:\n%s\n\nThe code:\n-----\n%s\n-----\n"
            "Explain in plain English, in at most 5 short lines, what is wrong and how to fix it. Plain text only: no Markdown."
            % (instruction, context))


def build_messages(mode, instruction, context, project_files=None, machine="", sizes=""):
    """The chat messages for a job (the instruction and context are validated)."""
    if mode in MATH_MODES:
        return [{"role": "system", "content": math_system_prompt(machine, mode not in (MODE_MATH_EXPLAIN, MODE_MATH_WHY))},
                {"role": "user", "content": build_math_request(mode, instruction, context, sizes)}]
    if mode in PROJECT_MODES:
        d = parse_descriptor(context)
        system = PASCAL_SYSTEM_PROMPT if language_of(d["file"]) == "pascal" else CODE_SYSTEM_PROMPT
        return [{"role": "system", "content": system},
                {"role": "user", "content": build_project_request(mode, instruction, d, project_files or [])}]
    if mode in CODE_MODES:
        return [{"role": "system", "content": CODE_SYSTEM_PROMPT},
                {"role": "user", "content": build_code_request(mode, instruction, context)}]
    if mode == MODE_WRITE:
        user = instruction
    elif mode == MODE_ASK:
        user = ("Here is my document, between the lines of dashes:\n-----\n%s\n-----\n\n%s"
                % (context, instruction))
    elif mode == MODE_REWRITE:
        user = ("Rewrite this paragraph. %s Reply with the rewritten paragraph only.\n\n%s"
                % (instruction or DEFAULT_REWRITE, context))
    else:
        user = ("Continue the following text from exactly where it stops, with about one "
                "paragraph. Reply with the continuation only; do not repeat any of the text."
                + (" %s" % instruction if instruction else "") + "\n\n" + context)
    return [{"role": "system", "content": SYSTEM_PROMPT}, {"role": "user", "content": user}]


def build_code_request(mode, instruction, context):
    if mode == MODE_CODE_EXPLAIN:
        return ("Turbo C 2.01 reports this message:\n%s\n\nThe code (each line starts with its line number):\n-----\n%s\n-----\n"
                "Explain in plain English, in at most 6 short lines, what is wrong and how to fix it. "
                "Plain text only: no code, no Markdown." % (instruction, context))
    if mode == MODE_CODE_FIX:
        return ("Turbo C 2.01 reports:\n%s\n\nThe code:\n-----\n%s\n-----\n"
                "Reply with the corrected function only (not the #include lines), complete and ready to replace the old one. %s"
                % (instruction or "no message (find and fix the mistakes)", context, CODE_ONLY))
    if mode == MODE_CODE_CHANGE:
        return ("Change the function below as the user asks: %s\n\nThe code:\n-----\n%s\n-----\n"
                "Reply with the new code that replaces that function, complete (it may add helper functions and #include lines "
                "before it). %s" % (instruction, context, CODE_ONLY))
    request = "Write Turbo C 2.01 C code for this request: %s\n" % instruction
    if context.strip():
        request += "The file starts with these lines:\n-----\n%s\n-----\n" % context
    return request + CODE_ONLY


def context_window(messages):
    """num_ctx for Ollama, or None for its default (changing it reloads the model)."""
    tokens = sum(len(m["content"]) for m in messages) // 3 + ANSWER_TOKENS_ALLOWANCE
    if tokens <= CONTEXT_WINDOW_DEFAULT:
        return None
    window = CONTEXT_WINDOW_DEFAULT
    while window < tokens and window < CONTEXT_WINDOW_MAX:
        window *= 2
    return window


# --- Markdown removal that only ever appends -------------------------------------
_LINE_START = [
    (re.compile(r"^\s{0,3}#{1,6}\s+"), ""),             # headings
    (re.compile(r"^(\s*)[*+]\s+"), r"\1- "),            # bullets
]
_INLINE = [
    (re.compile(r"\*\*(.+?)\*\*"), r"\1"),
    (re.compile(r"__(.+?)__"), r"\1"),
    (re.compile(r"`([^`]*)`"), r"\1"),
]
_FENCE = re.compile(r"^\s*```")
_LINE_MARKERS = re.compile(r"\s*[#*+]*\Z")


def clean_line(line):
    """Markdown off, printable ASCII (trailing spaces kept: a line released
    early may already end in one)."""
    for pattern, replacement in _LINE_START + _INLINE:
        line = pattern.sub(replacement, line)
    return files.to_c64_ascii(line)


def first_unclosed_marker(text):
    """Where the first "**", "__" or "`" without its closing partner starts."""
    open_at = {}
    position = 0
    while position < len(text):
        for marker in ("`", "**", "__"):
            if text.startswith(marker, position) and ("`" not in open_at or marker == "`"):
                if marker in open_at:
                    del open_at[marker]
                else:
                    open_at[marker] = position
                position += len(marker) - 1
                break
        position += 1
    return min(open_at.values(), default=None)


def safe_prefix(line):
    """The part of an unfinished line that later text cannot change: whole
    words before any unclosed marker, once the first word shows whether the
    line is a heading or a bullet."""
    head = line[:max(line.rfind(" "), 0)]
    marker = first_unclosed_marker(head)
    if marker is not None:
        head = head[:marker]
        head = head[:max(head.rfind(" "), 0)]
    words = head.split()
    if not words or (_LINE_MARKERS.match(words[0]) and len(words) < 2):
        return ""
    return clean_line(head)


_THINK_BLOCK = re.compile(r"<think>.*?(</think>|\Z)", re.S)
_THINKING_CLI = re.compile(r"^[ \t]*Thinking\.\.\.[ \t]*\n.*?(^[ \t]*\.\.\.done thinking\.[ \t]*\n?|\Z)", re.S | re.M)


def strip_reasoning(raw):
    """The model's text without its reasoning, however the model wrote it (XD93): <think>...</think>, the same with the opening tag missing (some chat templates
    put it in the prompt, so only the closing tag is in the answer), and the 'Thinking...' ... '...done thinking.' form of Ollama's own command line. An unfinished
    reasoning gives nothing yet. (Ollama's API normally keeps the reasoning in a field of its own, which chat.stream never reads.)"""
    if "</think>" in raw and "<think>" not in raw[:raw.index("</think>")]:
        raw = raw[raw.index("</think>") + len("</think>"):]
    raw = _THINK_BLOCK.sub("", raw)
    return _THINKING_CLI.sub("", raw)


class Answer:
    """The model's raw text in, the C64's append-only text out."""

    def __init__(self, space_before=False):
        self.raw = ""
        self.text = ""                      # released to the C64; only grows
        self.space_before = space_before    # continue: the context ends inside a line

    def add(self, piece):
        self.raw += piece
        self.release(final=False)

    def finish(self):
        self.release(final=True)

    def release(self, final):
        raw = strip_reasoning(self.raw)
        parts = raw.split("\n")
        finished, partial = (parts, "") if final else (parts[:-1], parts[-1])
        lines = [clean_line(line) for line in finished if not _FENCE.match(line)]
        if partial and not _FENCE.match(partial):
            lines.append(safe_prefix(partial))
        text = re.sub(r"\n{3,}", "\n\n", "\n".join(lines)).lstrip("\n")
        text = text.rstrip("\n")           # blank lines only once text follows
        if self.space_before and text[:1].isalnum():
            text = " " + text
        text = text[:ANSWER_CHARS_MAX]
        if text.startswith(self.text):
            self.text = text
        elif len(text) > len(self.text):    # should not happen; never take text back
            self.text += text[len(self.text):]


class CodeAnswer:
    """A code mode's answer: the model's lines as they are (tabs expanded, printable ASCII), without code fences, <think>
    or leading blank lines; a line is released only when it is complete. An explanation (comment=True) is wrapped to
    COMMENT_WIDTH, cleaned of Markdown and put inside /* ... */ (a "*/" in it is broken up). It ends with a newline."""

    def __init__(self, comment=False, pascal=False):
        self.raw = ""
        self.text = ""
        self.comment = comment
        self.pascal = pascal                # an explanation goes inside { ... } (a "}" in it becomes ")") instead of /* ... */

    def add(self, piece):
        self.raw += piece
        self.release(final=False)

    def finish(self):
        self.release(final=True)

    def release(self, final):
        raw = strip_reasoning(self.raw)
        parts = raw.split("\n")
        finished = parts if final else parts[:-1]
        lines = [line for line in finished if not _FENCE.match(line)]
        if self.comment:
            body = []
            for line in lines:
                line = clean_line(line).replace("}", ")") if self.pascal else clean_line(line).replace("*/", "* /")
                line = line.rstrip()
                body.extend(files.wrap_line(line, COMMENT_WIDTH) if line else [""])
            while body and not body[0].strip():
                body.pop(0)
            while body and not body[-1].strip():               # blank lines only before more text
                body.pop()
            if self.pascal:
                text = "\n".join(("{ " if i == 0 else "  ") + b for i, b in enumerate(body))
            else:
                text = "\n".join(("/* " if i == 0 else " * ") + b for i, b in enumerate(body)).replace(" * \n", " *\n")
            text = "\n".join(l.rstrip() for l in text.split("\n")) if body else ""
            if final:
                if self.pascal:
                    text = (text + "\n}\n") if body else "{ (no answer) }\n"
                else:
                    text = (text + "\n */\n") if body else "/* (no answer) */\n"
            elif body:
                text += "\n"
        else:
            lines = [files.to_c64_ascii(line).rstrip() for line in lines]
            while lines and not lines[0].strip():
                lines.pop(0)
            while lines and not lines[-1].strip():
                lines.pop()
            text = "".join(line + "\n" for line in lines)
        text = text[:ANSWER_CHARS_MAX]
        if text.startswith(self.text):
            self.text = text
        elif len(text) > len(self.text):    # should not happen; never take text back
            self.text += text[len(self.text):]


class Job:
    def __init__(self, job_id, mode, instruction_length, big=False):
        self.big = big
        self.context_max = CONTEXT_BIG_MAX if big else CONTEXT_MAX
        self.id = job_id
        self.mode = mode
        self.instruction_length = instruction_length
        self.data = bytearray()
        self.crc = 0xFFFF
        self.started = False
        self.generating = False
        self.failed = None                  # info text if the model failed
        self.stop = threading.Event()
        self.answer = None
        self.machine = ""                   # MathSmith: which machine asks (the #machine= line of its context, XD87)
        self.model = ""                     # the Ollama model the user chose in the program (#model=, XD89); empty: the server's
        self.sizes = ""                     # MathSmith fix: "x:100x1,A:100x2", the sizes of the variables at the error (#sizes=, XD90)
        self.listing = False                # #list=models: the answer is the installed models, not the model's
        self.touched = time.monotonic()


class Writer:
    def __init__(self, chat_service, log=None, root=None):
        self.chat = chat_service            # its Ollama address and model
        self.root = root                    # the storage root: where code/<project>/ is (project modes)
        self.log = log
        self.jobs = {}
        self.lock = threading.Lock()

    def note(self, text, *args):
        if self.log:
            self.log.info("AI " + text, *args)

    def expire(self):
        now = time.monotonic()
        for job_id, job in list(self.jobs.items()):
            if not job.generating and now - job.touched > JOB_IDLE_SECONDS:
                del self.jobs[job_id]

    # --- endpoints ---
    def begin(self, mode, instruction_length, big=False):
        if mode not in (MODE_WRITE, MODE_ASK, MODE_REWRITE, MODE_CONTINUE) + CODE_MODES + MATH_MODES \
                or not 0 <= instruction_length <= INSTRUCTION_MAX:
            return reply(files.STATUS_BAD_REQUEST)
        if not self.chat.model_available():
            return reply(STATUS_LLM_UNAVAILABLE)
        with self.lock:
            self.expire()
            if len(self.jobs) >= JOBS_MAX:
                oldest = min((j for j in self.jobs.values() if not j.generating),
                             key=lambda j: j.touched, default=None)
                if oldest is None:
                    return reply(STATUS_BUSY)
                del self.jobs[oldest.id]
            job_id = 0
            while job_id == 0 or job_id in self.jobs:
                job_id = secrets.randbits(32)
            self.jobs[job_id] = Job(job_id, mode, instruction_length, big)
        return reply(files.STATUS_OK, job_id)

    def data(self, job_id, offset, body):
        chunk = uploads.unwrap_post_body(body)
        with self.lock:
            job = self.jobs.get(job_id)
            if job is None:
                return reply(STATUS_UNKNOWN_JOB, job_id)
            job.touched = time.monotonic()
            if chunk is None or not 1 <= len(chunk) <= (uploads.RAW_CHUNK_MAX if job.big else uploads.UPLOAD_CHUNK_MAX) \
                    or job.started or len(job.data) + len(chunk) > INSTRUCTION_MAX + job.context_max:
                return reply(files.STATUS_BAD_REQUEST, job_id, len(job.data))
            if offset != len(job.data):
                return reply(STATUS_OUT_OF_ORDER, job_id, len(job.data))
            job.data += chunk
            job.crc = files.crc16(chunk, job.crc)
            return reply(files.STATUS_OK, job_id, len(job.data))

    def go(self, job_id, size, crc):
        with self.lock:
            job = self.jobs.get(job_id)
            if job is None:
                return reply(STATUS_UNKNOWN_JOB, job_id)
            job.touched = time.monotonic()
            received = len(job.data)
            if job.started:
                return reply(STATUS_BUSY, job_id, received)
            if size != received or crc != job.crc:
                del self.jobs[job_id]
                return reply(STATUS_CHECK_FAILED, job_id, received)
            text = job.data.decode("ascii", "replace")
            instruction = text[:job.instruction_length].strip()
            context = text[job.instruction_length:]
            if job.mode not in MATH_MODES:                                  # WordSmith's and CodeSmith's requests may start with #model= only (XD92)
                headers, context = split_headers(context, ("model",))
                job.model = headers.get("model", "")
            if job.mode in MATH_MODES:
                headers, context = split_headers(context)
                job.machine = headers.get("machine", "") if headers.get("machine", "") in MATH_PLATFORMS else ""
                job.model = headers.get("model", "")
                job.listing = headers.get("list") == "models"
                job.sizes = headers.get("sizes", "")
            needs_instruction = job.mode in (MODE_WRITE, MODE_ASK, MODE_CODE_EXPLAIN, MODE_CODE_WRITE, MODE_PROJ_EXPLAIN,
                                             MODE_PROJ_WRITE, MODE_CODE_CHANGE, MODE_PROJ_CHANGE,
                                             MODE_MATH_ASK, MODE_MATH_CODE, MODE_MATH_WHY, MODE_MATH_CHANGE)
            needs_context = job.mode not in (MODE_WRITE, MODE_CODE_WRITE, MODE_MATH_ASK, MODE_MATH_CODE)
            project_files = None
            if job.mode in PROJECT_MODES:
                descriptor = parse_descriptor(context)
                status = files.STATUS_BAD_REQUEST
                if descriptor is not None and self.root is not None:
                    try:
                        project_files = files.project_files(self.root, descriptor["project"])
                        if not any(n.casefold() == descriptor["file"].casefold() for n, _ in project_files):
                            status = files.STATUS_NOT_FOUND
                            project_files = None
                    except files.RequestError as e:
                        status = e.status
                if project_files is None:
                    del self.jobs[job_id]
                    return reply(status, job_id, received)
            if job.instruction_length > received or (needs_instruction and not instruction) \
                    or (needs_context and not context.strip()) or len(context) > job.context_max:
                del self.jobs[job_id]
                return reply(files.STATUS_BAD_REQUEST, job_id, received)
            models = None
            if job.model or job.listing:                                      # the user's choice of model is checked against what Ollama has (XD89)
                models = chat.installed_models(self.chat.ollama_url)
                if models is None or (job.model and job.model not in models):
                    self.note("%08x model %r is not available (Ollama has %s)", job_id, job.model,
                              "no answer" if models is None else "%d models" % len(models))
                    del self.jobs[job_id]
                    return reply(STATUS_LLM_UNAVAILABLE, job_id, received)
            job.started = job.generating = True
            if job.listing:                                              # the answer is the models: the server's default first, then every installed one
                job.answer = CodeAnswer()
                job.answer.add("\n".join([self.chat.model] + models) + "\n")
                job.answer.finish()
                job.generating = False
                self.note("%08x lists %d models", job_id, len(models))
                return reply(files.STATUS_OK, job_id, received)
            if job.mode in MATH_CODE_MODES:
                job.answer = CodeAnswer()
            elif job.mode in MATH_MODES:
                job.answer = Answer()
            elif job.mode in CODE_MODES:
                descriptor = parse_descriptor(context) if job.mode in PROJECT_MODES else None
                job.answer = CodeAnswer(comment=job.mode in (MODE_CODE_EXPLAIN, MODE_PROJ_EXPLAIN),
                                        pascal=descriptor is not None and language_of(descriptor["file"]) == "pascal")
            else:
                job.answer = Answer(space_before=job.mode == MODE_CONTINUE
                                    and context[-1:] not in ("", " ", "\n"))
        threading.Thread(target=self.generate, args=(job, instruction, context, project_files), daemon=True).start()
        self.note("%08x mode %d, %d+%d characters%s: %s", job_id, job.mode, len(instruction),
                  len(context), " (model %s)" % job.model if job.model else "", instruction)
        return reply(files.STATUS_OK, job_id, received)

    def read(self, job_id, offset, count):
        with self.lock:
            job = self.jobs.get(job_id)
            if job is None or not job.started:
                return READ_HEADER.pack(files.PROTOCOL_VERSION, STATUS_UNKNOWN_JOB, 1, 0, offset & 0xFFFF, 0, 0)
            job.touched = time.monotonic()
            text = job.answer.text.encode("ascii")
            generating = job.generating
            failed = job.failed
        if not 1 <= count <= READ_COUNT_MAX or offset > len(text):
            return READ_HEADER.pack(files.PROTOCOL_VERSION, files.STATUS_BAD_REQUEST, 1, 0, offset & 0xFFFF, 0, 0)
        if failed and not text:
            return READ_HEADER.pack(files.PROTOCOL_VERSION, STATUS_LLM_FAILED, 1, 0, offset, 0, 0)
        page = text[offset:offset + count]
        done = 0 if generating or offset + len(page) < len(text) else 1
        crc = files.crc16(text[:offset + len(page)])
        return READ_HEADER.pack(files.PROTOCOL_VERSION, files.STATUS_OK, done, len(text), offset,
                                len(page), crc) + page

    def stop(self, job_id):
        with self.lock:
            job = self.jobs.get(job_id)
            if job is None:
                return reply(STATUS_UNKNOWN_JOB, job_id)
            job.stop.set()
            return reply(files.STATUS_OK, job_id, len(job.data))

    # --- the model, in a background thread ---
    def generate(self, job, instruction, context, project_files=None):
        messages = build_messages(job.mode, instruction, context, project_files, job.machine, job.sizes)
        window = context_window(messages)

        def add(piece):
            with self.lock:
                job.answer.add(piece)
                if len(job.answer.text) >= ANSWER_CHARS_MAX:
                    return "(answer cut off)"
            return None

        info = chat.stream(self.chat.ollama_url, job.model or self.chat.model, messages, job.stop, add,
                           {"num_ctx": window} if window else None)
        with self.lock:
            job.answer.finish()
            job.failed = info if info and info.startswith("(error") else None
            job.generating = False
            job.touched = time.monotonic()
        self.note("%08x answered (%d characters)%s", job.id, len(job.answer.text), " " + info if info else "")
