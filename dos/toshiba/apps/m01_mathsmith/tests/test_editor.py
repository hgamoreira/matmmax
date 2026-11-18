"""M08: MathSmith's script editor in DOSBox-X: keys are injected through XTKEYS; the editor's screen is dumped before every key it reads
(XTDUMPEDIT: the last dump is the screen before the key that left it) and the full screen's at the end (XTDUMP)."""

import os
import unittest

from dosbox import decode_screen, run_program

EXE = os.environ.get("XT_EXE", "build/m01_mathsmith/MATHSMTH.EXE")
KEYS = {"ENTER": "1C0D", "ESC": "011B", "BKSP": "0E08", "UP": "4800", "DOWN": "5000", "LEFT": "4B00", "RIGHT": "4D00", "HOME": "4700", "END": "4F00",
        "PGUP": "4900", "PGDN": "5100", "DEL": "5300", "TAB": "0F09", "F7": "4100", "F2": "3C00", "F3": "3D00", "F4": "3E00", "F5": "3F00", "F6": "4000",
        "F10": "4400", "F1": "3B00", "ALTZ": "2C00", "CTRLHOME": "7700", "CTRLEND": "7500", "-": "-"}


def keys(*parts):
    """A name in KEYS is that key; any other string is typed."""
    return " ".join(KEYS[p] if p in KEYS else " ".join("%04X" % ord(c) for c in p) for p in parts)


def run(*parts, files=None, limit=90):
    """The keys are typed, then F10 (quits MathSmith at its prompt; unread if the program has quit already)."""
    all_files = {"KEYS.TXT": keys(*parts, "F10").encode()}
    all_files.update(files or {})
    r = run_program(EXE, [], before=["set XTKEYS=C:\\KEYS.TXT", "set XTDUMPEDIT=C:\\ED.BIN"], files=all_files, time_limit=limit,
                    read_files=["ED.BIN", "HELLO.M", "BAD.M", "TW.M", "ASK.M", "COPY.M", "MINE.M", "OTHER.M"])
    assert r.screen is not None, "the program did not leave a screen dump"
    r.rows = ["".join(cell.char for cell in row).rstrip() for row in r.screen]
    r.ed = None
    if r.files.get("ED.BIN"):
        r.ed = ["".join(cell.char for cell in row).rstrip() for row in decode_screen(r.files["ED.BIN"], 25)]
    r.hello = (r.files.get("HELLO.M") or b"").replace(b"\r\n", b"\n")
    return r


def history(r):
    return [row for row in r.rows[1:23] if row]


class Editor(unittest.TestCase):
    def test_a_new_file_is_typed_and_saved(self):
        r = run("edit hello", "ENTER", "x = 3", "ENTER", "y = x * 2", "F2", "RIGHT", "ESC")
        self.assertEqual(r.hello, b"x = 3\ny = x * 2")
        self.assertIn("hello.m", r.ed[0])
        self.assertTrue(r.ed[1].startswith("x = 3"))
        self.assertTrue(r.ed[2].startswith("y = x * 2"))
        self.assertIn("F2 Save", r.ed[24])
        self.assertIn("Esc Quit", r.ed[24])

    def test_the_status_line_and_the_question_on_leaving(self):
        r = run("edit hello", "ENTER", "ab", "ENTER", "cde", "F10", "d")
        self.assertIn("hello.m *", r.ed[0])
        self.assertIn("Ln 2", r.ed[0])
        self.assertIn("Col 4", r.ed[0])
        self.assertIn("S)ave", r.ed[24])
        self.assertIn("changed", r.ed[24])

    def test_a_file_is_loaded_and_edited_in_the_middle(self):
        r = run("edit hello", "ENTER", "DOWN", "END", "!", "UP", "HOME", ">", "F2", "ESC", files={"HELLO.M": b"one\r\ntwo\r\nthree\r\n"})
        self.assertEqual(r.hello, b">one\ntwo!\nthree\n")

    def test_enter_keeps_the_indent_and_tab_goes_to_a_tab_stop(self):
        r = run("edit hello", "ENTER", "if x", "ENTER", "TAB", "y = 1", "ENTER", "z = 2", "F2", "ESC")
        self.assertEqual(r.hello, b"if x\n    y = 1\n    z = 2")

    def test_backspace_joins_lines_and_the_line_number_follows(self):
        r = run("edit hello", "ENTER", "ab", "ENTER", "cd", "ENTER", "ef", "HOME", "BKSP", "F10", "d")
        self.assertIn("Ln 2", r.ed[0])                      # joined: "ab", "cdef"
        self.assertTrue(r.ed[1].startswith("ab"))
        self.assertTrue(r.ed[2].startswith("cdef"))
        r = run("edit hello", "ENTER", "ab", "ENTER", "cd", "UP", "END", "DEL", "F10", "d")
        self.assertTrue(r.ed[1].startswith("abcd"))
        self.assertIn("Ln 1", r.ed[0])

    def test_leaving_with_changes_asks_and_discard_keeps_the_old_file(self):
        r = run("edit hello", "ENTER", "zzz", "ESC", "d", files={"HELLO.M": b"old\r\n"})
        self.assertEqual(r.hello, b"old\n")
        self.assertIn("changed", r.ed[24])

    def test_save_and_leave_from_the_question(self):
        r = run("edit hello", "ENTER", "new ", "ESC", "s", files={"HELLO.M": b"old\r\n"})
        self.assertEqual(r.hello, b"new old\n")

    def test_esc_at_the_question_stays_in_the_editor(self):
        r = run("edit hello", "ENTER", "q", "ESC", "ESC", "F2", "ESC")
        self.assertEqual(r.hello, b"q")

    def test_find_and_go_to_line(self):
        text = b"alpha\r\nbeta\r\ngamma\r\ndelta\r\nGamma again\r\n"
        r = run("edit hello", "ENTER", "F3", "gam", "ENTER", "ESC", files={"HELLO.M": text})
        self.assertIn("Ln 3", r.ed[0])
        self.assertIn("Found", r.ed[24])
        r = run("edit hello", "ENTER", "F3", "gam", "ENTER", "F3", "ENTER", "ESC", files={"HELLO.M": text})
        self.assertIn("Ln 5", r.ed[0])                      # Enter again repeats the same search: the next one
        r = run("edit hello", "ENTER", "F3", "gam", "ENTER", "F3", "ENTER", "F3", "ENTER", "ESC", files={"HELLO.M": text})
        self.assertIn("Found (from the top)", r.ed[24])     # past the last one, it starts again from the top
        self.assertIn("Ln 3", r.ed[0])
        r = run("edit hello", "ENTER", "F3", "nothing", "ENTER", "ESC", files={"HELLO.M": text})
        self.assertIn("Not found", r.ed[24])
        r = run("edit hello", "ENTER", "F4", "4", "ENTER", "ESC", files={"HELLO.M": text})
        self.assertIn("Ln 4", r.ed[0])

    def test_a_long_line_scrolls_sideways(self):
        r = run("edit hello", "ENTER", "x = [" + "1 " * 60 + "];", "F10", "d")
        self.assertIn("Col 128", r.ed[0])
        self.assertTrue(r.ed[1].endswith("1 1 ];"))
        self.assertLessEqual(len(r.ed[1]), 80)
        r = run("edit hello", "ENTER", "x = [" + "1 " * 60 + "];", "HOME", "F10", "d")
        self.assertTrue(r.ed[1].startswith("x = [1 1"))

    def test_run_shows_the_output_and_f5_comes_back(self):
        r = run("edit hello", "ENTER", "x = 4;", "ENTER", "disp(x * 5)", "F5", "a", "ESC")
        self.assertEqual(r.hello, b"x = 4;\ndisp(x * 5)")
        self.assertIn("20", history(r))
        self.assertIn("hello.m", r.ed[0])                     # a key after the run: back in the editor

    def test_run_stops_at_the_line_of_the_error(self):
        script = b"a = 1;\r\nb = 2;\r\nc = a + zz;\r\nd = 4;\r\n"
        r = run("edit bad", "ENTER", "F5", "a", "ESC", files={"BAD.M": script})
        self.assertIn("Ln 3", r.ed[0])
        self.assertIn("error in line 3", r.ed[24])
        self.assertIn("zz", r.ed[24])
        self.assertIn("bad.m", r.ed[0])

    def test_esc_after_a_run_stays_at_the_prompt(self):
        r = run("edit hello", "ENTER", "disp(7)", "F5", "ESC", "x = 2", "ENTER")
        self.assertIn("x = 2", history(r))
        self.assertEqual(r.rows[0].count("any key"), 0)

    def test_an_edited_function_is_used_by_the_next_call(self):
        f = b"function y = tw(x)\r\ny = 2 * x;\r\nend\r\n"
        r = run("tw(5)", "ENTER", "edit tw", "ENTER", "DOWN", "END", "BKSP", "BKSP", "BKSP", "BKSP", "BKSP", "BKSP", "3 * x;", "F2", "ESC",
                "tw(5)", "ENTER", files={"TW.M": f})
        h = history(r)
        self.assertIn("ans = 10", h)
        self.assertIn("ans = 15", h)

    def test_a_script_that_is_too_big_is_refused(self):
        big = b"".join(b"x%d = %d;\r\n" % (i, i) for i in range(2500))
        r = run("edit hello", "ENTER", files={"HELLO.M": big})
        self.assertTrue(any("cannot edit hello.m: That file is too big" in row for row in history(r)), history(r))

    def test_a_name_dos_does_not_accept_is_refused(self):
        r = run("edit averylongscriptname", "ENTER")
        self.assertTrue(any("not a name DOS accepts" in row for row in history(r)), history(r))
        r = run("edit con", "ENTER")
        self.assertTrue(any("not a name DOS accepts" in row for row in history(r)), history(r))

    def test_the_edit_command_in_line_mode_says_no(self):
        r = run_program(EXE, [], args="/L < IN.TXT > OUT.TXT", files={"IN.TXT": b"edit foo\r\nx = 1\r\nquit\r\n"}, read_files=["OUT.TXT"], time_limit=60)
        out = r.files["OUT.TXT"].decode("latin1")
        self.assertIn("the editor needs the full screen", out)
        self.assertIn("x = 1", out)

    def test_undo_without_an_assistant_change(self):
        r = run("edit hello", "ENTER", "ALTZ", "ESC")
        self.assertIn("Nothing to undo", r.ed[24])

    def test_a_bad_line_number_is_refused(self):
        r = run("edit hello", "ENTER", "F4", "0", "ENTER", "ESC")
        self.assertIn("A line number", r.ed[24])


def text_of(r, name):
    return (r.files.get(name) or b"").replace(b"\r\n", b"\n")


class SaveAs(unittest.TestCase):
    def test_the_text_is_saved_under_another_name_and_the_old_file_stays(self):
        r = run("edit ask", "ENTER", "END", "!", "F7", "BKSP", "BKSP", "BKSP", "mine", "ENTER", "ESC", files={"ASK.M": b"x = 1\r\n"})
        self.assertEqual(text_of(r, "MINE.M"), b"x = 1!\n")
        self.assertEqual(text_of(r, "ASK.M"), b"x = 1\n")
        self.assertIn("mine.m", r.ed[0])
        self.assertIn("Saved as mine.m", r.ed[24])
        self.assertNotIn("*", r.ed[0])

    def test_the_new_name_is_the_one_edited_and_run_from_then_on(self):
        r = run("edit hello", "ENTER", "disp(11)", "F7", "BKSP", "BKSP", "BKSP", "BKSP", "BKSP", "copy", "ENTER", "F5", "a", "ESC")
        self.assertEqual(text_of(r, "COPY.M"), b"disp(11)")
        self.assertFalse(r.files.get("HELLO.M"))
        self.assertIn("11", history(r))
        self.assertIn("copy.m", r.ed[0])

    def test_an_existing_name_asks_before_it_is_replaced(self):
        files = {"HELLO.M": b"new\r\n", "OTHER.M": b"old\r\n"}
        r = run("edit hello", "ENTER", "F7", "BKSP", "BKSP", "BKSP", "BKSP", "BKSP", "other", "ENTER", "n", "ESC", files=files)
        self.assertEqual(text_of(r, "OTHER.M"), b"old\n")
        self.assertIn("Not saved", r.ed[24])
        self.assertIn("hello.m", r.ed[0])
        r = run("edit hello", "ENTER", "F7", "BKSP", "BKSP", "BKSP", "BKSP", "BKSP", "other", "ENTER", "y", "ESC", files=files)
        self.assertEqual(text_of(r, "OTHER.M"), b"new\n")
        self.assertIn("Saved as other.m", r.ed[24])

    def test_a_bad_name_is_refused_and_esc_cancels(self):
        files = {"HELLO.M": b"new\r\n"}
        r = run("edit hello", "ENTER", "F7", "BKSP", "BKSP", "BKSP", "BKSP", "BKSP", "a b", "ENTER", "ESC", files=files)
        self.assertIn("Not a name DOS accepts", r.ed[24])
        self.assertIn("hello.m", r.ed[0])
        r = run("edit hello", "ENTER", "F7", "ESC", "ESC", files=files)
        self.assertIn("hello.m", r.ed[0])
        self.assertNotIn("Saved as", r.ed[24])

    def test_f7_is_on_the_key_bar(self):
        r = run("edit hello", "ENTER", "RIGHT", "ESC")
        self.assertIn("F7 Save as", r.ed[24])


class InstructionBox(unittest.TestCase):
    """What the assistant is asked is typed in a box of three rows, wrapped, not on one line."""

    def test_a_long_request_is_shown_wrapped(self):
        text = "make the script print every intermediate result and then plot the final vector with a title and labels on both axes, please"
        r = run("edit hello", "ENTER", "F6", "c", text, "ENTER", "ESC")
        self.assertIn("Change how?", r.ed[20])
        self.assertEqual(r.ed[21], text[:80])
        self.assertEqual(r.ed[22], text[80:])
        self.assertIn("%d of 199 characters" % len(text), r.ed[24])

    def test_editing_inside_the_box_and_the_limit(self):
        r = run("edit hello", "ENTER", "F6", "w", "abcdef", "LEFT", "LEFT", "BKSP", "X", "HOME", "DEL", "ENTER", "ESC")
        self.assertEqual(r.ed[21], "bcXef")
        r = run("edit hello", "ENTER", "F6", "w", "z" * 230, "ENTER", "ESC")
        self.assertIn("199 of 199 characters", r.ed[24])

    def test_esc_cancels_and_the_text_comes_back_whole(self):
        r = run("edit hello", "ENTER", "x", "F2", "F6", "w", "something long", "ESC", "ESC")
        self.assertEqual(r.hello, b"x")
        self.assertTrue(r.ed[1].startswith("x"))
        self.assertEqual(r.ed[21], "")                         # the box is gone and the text rows are drawn again


class HelpViewer(unittest.TestCase):
    """The help is longer than the screen: `help` and F1 show it in the editor, read only."""

    def test_help_opens_in_the_viewer(self):
        r = run("help", "ENTER", "ESC")
        self.assertIn("MathSmith help (read only)", r.ed[0])
        self.assertIn("Lines 1-23 of", r.ed[0])
        self.assertTrue(r.ed[1].startswith("MathSmith help"))
        self.assertIn("THE BASICS", r.ed[3])
        self.assertIn("Esc Close", r.ed[24])
        self.assertIn(">> help", history(r))                       # the prompt is back, and the history is not full of help
        self.assertFalse(any("THE BASICS" in row for row in history(r)))

    def test_f1_opens_it_and_f1_closes_it(self):
        r = run("F1", "F1", "x = 1", "ENTER")
        self.assertIn("x = 1", history(r))

    def test_page_down_and_the_end(self):
        r = run("help", "ENTER", "PGDN", "ESC")
        self.assertNotIn("THE BASICS", "".join(r.ed[1:24]))
        self.assertIn("Lines 23-45 of", r.ed[0])
        r = run("help", "ENTER", "CTRLEND", "ESC")
        self.assertTrue(any("PgUp and PgDn scroll back" in row for row in r.ed[1:24]), r.ed)
        r = run("help", "ENTER", "PGDN", "PGDN", "PGUP", "ESC")
        self.assertIn("Lines 23-45 of", r.ed[0])
        r = run("help", "ENTER", "CTRLEND", "CTRLHOME", "ESC")
        self.assertIn("Lines 1-23 of", r.ed[0])

    def test_nothing_can_be_changed(self):
        r = run("help", "ENTER", "x", "ENTER", "BKSP", "DEL", "TAB", "F2", "ESC")
        self.assertIn("Read only", r.ed[24])
        self.assertTrue(r.ed[1].startswith("MathSmith help"))
        self.assertNotIn("*", r.ed[0])
        self.assertNotIn("Saved", r.ed[24])
        self.assertFalse(r.files.get("HELP.M"))

    def test_find_in_the_help(self):
        r = run("help", "ENTER", "F3", "polynomials", "ENTER", "ESC")
        self.assertIn("Found", r.ed[24])
        self.assertTrue(any("Polynomials:" in row for row in r.ed[1:24]), r.ed)

    def test_no_line_is_wider_than_the_screen(self):
        r = run("help", "ENTER", "CTRLEND", "ESC")
        self.assertTrue(all(len(row) <= 80 for row in r.ed))


if __name__ == "__main__":
    unittest.main()
