"""Run a DOS program headlessly in DOSBox-X and read back its text screen.

The program is copied into a temporary directory mounted as C:, keys are
injected with DOSBox-X's AUTOTYPE (through the emulated keyboard, so the
program's BIOS/DOS keyboard path is really exercised), and the program's
XTDUMP hook writes the 80x25 text screen to C:\\SCREEN.BIN before exit.
"""

import re
import shutil
import subprocess
import tempfile
from dataclasses import dataclass
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
BASE_CONF = REPO_ROOT / "dosbox-x.conf"
ROWS, COLS = 25, 80


@dataclass
class Cell:
    char: str
    attr: int


@dataclass
class RunResult:
    exit_code: int | None
    screen: list[list[Cell]] | None
    files: dict | None = None   # name -> bytes, the files asked for with read_files
    tree: list | None = None    # every file and folder on C: afterwards, as upper-case relative paths

    def row_text(self, row):
        return "".join(c.char for c in self.screen[row]).rstrip()

    def find(self, text):
        """(row, col) of the first occurrence of text on screen, or None."""
        for row in range(len(self.screen)):
            col = "".join(c.char for c in self.screen[row]).find(text)
            if col >= 0:
                return row, col
        return None

    def attrs_of(self, text):
        """Set of attributes used by the cells of the first occurrence of text."""
        pos = self.find(text)
        if pos is None:
            raise AssertionError(f"{text!r} not on screen:\n{self.dump()}")
        row, col = pos
        return {self.screen[row][c].attr for c in range(col, col + len(text))}

    def dump(self):
        return "\n".join(self.row_text(r) for r in range(len(self.screen)))


def run_program(exe, keys=(), wait=1.5, pace=0.3, time_limit=30, *, cputype=None, machine=None,
                extra_conf="", before=(), files=None, read_files=(), args="", readonly=(), rows=ROWS):
    """Run exe (a path) with the given AUTOTYPE key names; return RunResult.

    cputype replaces the base config's CPU core (the NE2000 only works
    with a 386 core, see PROGRESS.md); extra_conf is appended config
    text (e.g. an [ne2000] section); before are DOS commands run before
    the program; files {name: bytes} are put on C: first (a name ending in
    "/" is an empty folder; names may include folders); readonly names are
    made read-only; read_files are names (or folder/name paths) read back
    from C: afterwards into RunResult.files, and RunResult.tree lists
    everything left on C:; args are appended to the program's command line.

    rows is the height of the program's screen dump (25 text rows; the VGA editor has 30).

    exit_code is 0 or 1 (DOS errorlevel 0 / not 0), or None if the program
    did not finish. screen is None if the program wrote no dump.
    """
    exe = Path(exe)
    with tempfile.TemporaryDirectory(prefix="xtrun") as tmp:
        tmp = Path(tmp)
        shutil.copy(exe, tmp / exe.name)
        for name, data in (files or {}).items():
            if name.endswith("/"):
                (tmp / name).mkdir(parents=True, exist_ok=True)
                continue
            (tmp / name).parent.mkdir(parents=True, exist_ok=True)      # names may include folders
            (tmp / name).write_bytes(data)
        for name in readonly:
            (tmp / name).chmod(0o444)
        autoexec = [
            f"mount c {tmp}",
            "c:",
            r"set XTDUMP=C:\SCREEN.BIN",
        ]
        autoexec += list(before)
        if keys:
            autoexec.append(f"autotype -w {wait} -p {pace} " + " ".join(keys))
        autoexec += [
            (exe.name + " " + args).strip(),
            "if errorlevel 1 echo 1 > RC.TXT",
            "if not errorlevel 1 echo 0 > RC.TXT",
            "exit",
        ]
        conf = tmp / "run.conf"
        base = BASE_CONF.read_text()
        if cputype is not None:
            base = re.sub(r"(?m)^cputype=.*$", f"cputype={cputype}", base)
        if machine is not None:                  # e.g. "vgaonly" for the VGA programs
            base = re.sub(r"(?m)^machine=.*$", f"machine={machine}", base)
        conf.write_text(base + "\n" + extra_conf + "\n[autoexec]\n" + "\n".join(autoexec) + "\n")
        # DOSBox-X ignores SIGTERM; -time-limit plus SIGKILL (via run's timeout) bound it.
        try:
            subprocess.run(
                ["dosbox-x", "-conf", str(conf), "-fastlaunch", "-nogui", "-silent",
                 "-time-limit", str(time_limit)],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                timeout=time_limit + 10, check=False,
            )
        except subprocess.TimeoutExpired:
            pass        # the emulator hung (it does now and then under load): killed; the caller sees no screen
        rc_file = tmp / "RC.TXT"
        rc_text = rc_file.read_text().strip() if rc_file.exists() else ""
        exit_code = int(rc_text) if rc_text.isdigit() else None
        dump_file = tmp / "SCREEN.BIN"
        screen = decode_screen(dump_file.read_bytes(), rows) if dump_file.exists() else None
        everything = {str(p.relative_to(tmp)).upper(): p for p in tmp.rglob("*")}     # DOS names have no case
        got = {}
        for n in read_files:
            path = everything.get(n.upper().replace("\\", "/"))
            if path is not None and path.is_file():
                got[n] = path.read_bytes()
        return RunResult(exit_code, screen, got, sorted(everything))


def decode_screen(data, rows=ROWS):
    if len(data) != rows * COLS * 2:
        raise AssertionError(f"screen dump is {len(data)} bytes, expected {rows * COLS * 2}")
    cells = [Cell(bytes([data[i]]).decode("cp437"), data[i + 1]) for i in range(0, len(data), 2)]
    return [cells[r * COLS:(r + 1) * COLS] for r in range(rows)]
