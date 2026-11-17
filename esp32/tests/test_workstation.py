"""E02 runs the complete E01 numeric suite plus foreground app lifecycle checks."""
import re
import test_mathsmith as math_tests
from run_workstation import BUILD


class WorkstationFirmware(math_tests.MatmMaxFirmware):
    build_dir = BUILD
    banner = "MatmMax E02"

    def open_demo(self, c, arguments="", ending=b"\r"):
        c.send((":app demo" + (" " + arguments if arguments else "")).encode() + ending)
        output = c.read_until(b"demo> ")
        self.assertIn("Demo program", output)
        return output

    def test_launcher_list_unknown_and_usage(self):
        c, _ = self.start()
        c.command("x=41;")
        self.assertIn("demo - Launcher test program", c.command(":apps"))
        self.assertIn("Unknown application", c.command(":app missing"))
        self.assertIn("Usage: :app", c.command(":app"))
        self.assertIn("Usage: :apps", c.command(":apps extra"))
        self.assertIn("Application name too long", c.command(":app " + "x"*32))
        self.assertIn("Unknown platform command", c.command(":application"))
        self.near(self.numbers(c, "x+1"), [42])

    def test_workspace_survives_program_and_arguments(self):
        c, _ = self.start()
        c.command("x=41; A=[1 2;3 4]; f=@(t) t+x;")
        self.assertIn("Arguments: hello world", self.open_demo(c, "hello world"))
        c.send(b"x=999\r")
        self.assertIn("Demo echo: x=999", c.read_until(b"demo> "))
        c.send(b"exit\r")
        self.assertIn("Returned to MatmMax; workspace kept", c.read_until())
        self.near(self.numbers(c, "x+1"), [42])
        self.near(self.numbers(c, "A(2,1)"), [3])
        self.near(self.numbers(c, "f(1)"), [42])

    def test_cancel_and_error_keep_workspace(self):
        c, _ = self.start()
        c.command("x=41;")
        for keys, expected in ((b"\x1b", "Program cancelled"),
                               (b"fail\r", "Program returned an error")):
            self.open_demo(c)
            c.send(keys)
            self.assertIn(expected, c.read_until())
            self.near(self.numbers(c, "x+1"), [42])

    def test_repeated_launches_release_arena(self):
        c, _ = self.start()
        c.command("x=41;")
        self.near(self.numbers(c, "x"), [41])
        before = c.command(":mem")
        for keys in (b"exit\r", b"\x1b", b"fail\r") * 4:
            self.open_demo(c)
            c.send(keys)
            c.read_until()
        after = c.command(":mem")
        for pattern in (r"Engine payload: used=(\d+)", r"external=(\d+) bytes"):
            self.assertEqual(re.search(pattern, before).group(1),
                             re.search(pattern, after).group(1))

    def test_app_launch_waits_for_complete_math_block(self):
        c, _ = self.start()
        c.send(b"for k=1:3\r")
        c.read_until(b"..> ")
        c.send(b":app demo\r")
        output = c.read_until(b"..> ")
        self.assertIn("Finish or abort", output)
        self.assertNotIn("Demo program", output)
        c.send(b"disp(k)\r")
        c.read_until(b"..> ")
        self.assertIn("1\n2\n3\n", c.command("end"))
        c.command("apps=7; demo=8;")
        self.near(self.numbers(c, "apps+demo"), [15])

    def test_input_boundaries_editing_and_overflow(self):
        c, _ = self.start()
        c.command("x=41;")
        self.open_demo(c, ending=b"\r\n")
        c.send(b"hellp\bo\r\n")
        self.assertIn("Demo echo: hello", c.read_until(b"demo> "))
        c.send(b"a"*128 + b"\r\n")
        self.assertIn("Demo line too long; discarded", c.read_until(b"demo> "))
        c.send(b"exit\r\n")
        self.assertIn("Returned to MatmMax", c.read_until())
        # Match an actual result, tolerating a late empty CRLF submission.
        c.send(b"fprintf('MARK%.17gEND\\n',x)\n")
        output = c.read_until(b"END\r\n")
        self.assertIn("MARK41END", output)
        c.read_until()

    def test_launcher_allocation_failure_without_psram(self):
        c, boot = self.start(psram=False)
        self.assertIn("PSRAM: unavailable", boot)
        self.assertIn("demo", c.command(":apps"))
        self.assertIn("Not enough memory", c.command(":app demo"))
        self.assertIn("SDK:", c.command(":info"))
