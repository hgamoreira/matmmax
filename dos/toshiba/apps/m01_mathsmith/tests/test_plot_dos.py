"""Plots on the 8086 build in DOSBox-X's CGA machine: the video memory after the plot is drawn must equal the portable renderer's bitmap (the frame, the ticks, the
curves; not the letters, which the BIOS draws). This is the golden idea of XD72 applied to the display."""

import os
import sys
import tempfile
import unittest
from pathlib import Path

from dosbox import run_program

sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_engine as T  # noqa: E402

EXE = os.environ.get("XT_EXE", "build/m01_mathsmith/MATHSMTH.EXE")


def vram_rows(data):
    """The 200 pixel rows of mode 6 as strings of # and . (even rows first in memory, then the odd rows)."""
    rows = []
    for y in range(200):
        base = (0x2000 if y & 1 else 0) + (y >> 1) * 80
        rows.append("".join("#" if data[base + (x >> 3)] & (0x80 >> (x & 7)) else "." for x in range(640)))
    return rows


def dos_plot(lines, machine="cga"):
    script = ("\r\n".join(lines) + "\r\nquit\r\n").encode()
    files = {"IN.TXT": script, "KEYS.TXT": b"0020 0020 0020"}
    r = run_program(EXE, [], args="/L < IN.TXT > OUT.TXT", before=["set XTKEYS=C:\\KEYS.TXT", "set XTPLOTDUMP=C:\\PLOT.BIN"], files=files,
                    read_files=["OUT.TXT", "PLOT.BIN"], machine=machine, time_limit=120)
    out = (r.files.get("OUT.TXT") or b"").decode("latin1").replace("\r\n", "\n")
    return out, r.files.get("PLOT.BIN")


def native_rows(lines):
    m = T.Session()
    for line in lines:
        m.run(line)
    rows = m.plot()[0]
    m.close()
    return rows


class PlotDos(unittest.TestCase):
    def same_in_the_frame(self, lines):
        out, dump = dos_plot(lines)
        self.assertIsNotNone(dump, out)
        self.assertEqual(len(dump), 0x4000)
        dos = vram_rows(dump)
        nat = native_rows(lines)
        diffs = [(x, y) for y in range(16, 176) for x in range(72, 632) if dos[y][x] != nat[y][x]]
        self.assertEqual(diffs[:10], [], "pixels that differ (x, y): the 8086 build against the portable renderer")
        return dos, out

    def test_a_curve_with_a_grid(self):
        dos, out = self.same_in_the_frame(["x = 0:0.25:6.28; plot(x, sin(x)); grid on"])
        self.assertGreater(sum(r.count("#") for r in dos), 800)

    def test_several_curves_and_styles(self):
        self.same_in_the_frame(["x = 1:12; plot(x, x, 'o', x, x.^2/12, '--', x, 12 - x, ':'); hold on"])

    def test_limits_and_clipping(self):
        self.same_in_the_frame(["x = 0:40; plot(x, x.^2); axis([5 30 0 400])"])

    def test_the_letters_are_drawn_by_the_bios(self):
        dos, out = self.same_in_the_frame(["plot(0:10, 0:10); title('Line')"])
        title_area = sum(dos[y][x] == "#" for y in range(0, 8) for x in range(8 * 36, 8 * 44))
        tick_area = sum(dos[y][x] == "#" for y in range(176, 192) for x in range(0, 640))
        self.assertGreater(title_area, 10)                         # the word Line
        self.assertGreater(tick_area, 40)                          # the numbers under the axis

    def test_a_display_without_graphics_says_so(self):
        out, dump = dos_plot(["plot(1:3, 1:3)"], machine="mda")
        self.assertIsNone(dump)
        self.assertIn("cannot show graphics", out)


if __name__ == "__main__":
    unittest.main()
