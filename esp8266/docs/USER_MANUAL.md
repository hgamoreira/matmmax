# MathSmith Light — ESP8266 user manual

**L00 · Wemos D1 mini · 15 November 2026**

For the separate L01 browser/GPIO candidate, see the
[L01 user manual](USER_MANUAL_L01.md). This document retains L00's supported
features and serves as the math-language reference for both builds.

MathSmith Light turns the ESP8266 into a small mathematical workstation.
You can calculate, keep variables, manipulate matrices, solve equations and
write short programs. The current console uses a computer connected by USB
serial. Calculations run on the ESP8266; the computer supplies the terminal.

This manual describes **the implemented L00 firmware**. Wi-Fi is deliberately
off. There is no AI connection, file storage, app launcher, FFT, graphical plot
output, OLED driver or PS/2 keyboard driver in this version. Programs are
entered as MathSmith statements at the prompt; separate binaries cannot be
loaded. The OLED and PS/2 console is a planned extension.

Boot, serial interaction and the 4×4 eigenvalue example below have been
confirmed on the user's board. Other examples are checked against the native
build of the light engine; this does not establish their physical-board timing
or memory margins. The complete L00 hardware procedure remains open.

## Contents

1. [Connect and start](#1-connect-and-start)
2. [Your first calculations](#2-your-first-calculations)
3. [The console and its commands](#3-the-console-and-its-commands)
4. [Numbers, variables and operators](#4-numbers-variables-and-operators)
5. [Vectors, matrices and indexing](#5-vectors-matrices-and-indexing)
6. [Solving systems and finding eigenvalues](#6-solving-systems-and-finding-eigenvalues)
7. [Statistics and polynomials](#7-statistics-and-polynomials)
8. [Functions and numerical tools](#8-functions-and-numerical-tools)
9. [Writing short programs](#9-writing-short-programs)
10. [Text and timing](#10-text-and-timing)
11. [Memory and limits](#11-memory-and-limits)
12. [Troubleshooting](#12-troubleshooting)
13. [Firmware, peripherals and further reading](#13-firmware-peripherals-and-further-reading)

## 1. Connect and start

Connect the D1 mini to the computer with a USB data cable. On this Linux
system the serial port has been `/dev/ttyUSB0`; its name can change.
From the repository root, open the installed firmware's console:

```sh
esp8266/scripts/mathsmith.sh monitor /dev/ttyUSB0
```

The application uses **115200 baud, 8 data bits, no parity, 1 stop bit** and
no hardware flow control. The supplied monitor leaves DTR and RTS inactive.
Close any other program using this serial port before connecting.

Press the board's Reset button if you need to see the banner:

```text
MathSmith Light L00 - ESP8266 D1 mini
...
Ready. Type help.
>>
```

The ESP8266 boot ROM prints at **74880 baud** before the application starts.
A short burst of unreadable characters at 115200 during reset is expected.
Repeated boot messages or crashes are different: see troubleshooting.

**Type `help` and press Enter** to see the port's short help. To leave the
computer's supplied serial monitor, press **Ctrl+]**. The firmware's `exit`
and `quit` commands keep MathSmith running and preserve the workspace.

### Reading the examples

Command boxes below contain exactly what to type, without prompt characters.
Enter each line separately and wait for its prompt. Indentation in programs
is optional. Result boxes show selected output; the terminal also echoes your
input and prints prompts. Each example starts with a fresh workspace, so it
can be tried independently. **`:reset` discards existing work.**

Keep copies of useful programs on the computer. The board stores its workspace
in RAM: reset, reboot, power loss or reflashing loses it. Closing a serial
monitor can also reset some boards through its control-line behaviour.

## 2. Your first calculations

```mathsmith
:reset
2+3*4
x=12;
x^2
sin(pi/6)
sqrt(2)
```

Selected results:

```text
ans = 14
ans = 144
ans = 0.5000
ans = 1.4142
```

`x=12` assigns a value. A final semicolon suppresses its printed result.
An expression without an assignment stores its result in `ans`. Trigonometric
angles are in **radians**; multiply degrees by `pi/180` to convert them.

To see more digits:

```mathsmith
:reset
format long
sqrt(2)
fprintf('%.15g\n',sqrt(2))
format short
```

```text
ans = 1.41421356
1.4142135623731
```

`format short` normally shows four decimal places; `format long` shows eight.
Whole numbers may print without decimals, and very large or small values use
exponent notation. Display formatting does not change calculation precision.

## 3. The console and its commands

| Input | Effect |
|---|---|
| Enter | Submit the current line. |
| Backspace or Delete | Erase the last character on the input line. |
| Esc or Ctrl+C | Discard input or an unfinished block; cooperatively cancel a running calculation. |
| `abort` | Discard an unfinished block and return to `>>`. |
| `help`, `:help` | Show the L00 console help; `:help` also works during block entry. |
| `who` | List stored variables and functions. |
| `clear A` | Remove the variable or function named `A`. |
| `clear` or `clear all` | Remove all variables and functions. |
| `format short`, `format long` | Select result display precision. |
| `:info`, `:mem` | Show board and memory diagnostics. |
| `:reset` | Create a fresh MathSmith workspace. |
| `:reboot` | Restart the board. |
| `exit`, `quit` | Keep the firmware's main MathSmith prompt and workspace. |
| Ctrl+] | Leave the supplied computer-side serial monitor. |

`>>` means ready for a new command. `..>` means a block is unfinished: add
its remaining lines and close it with `end`, or cancel it. Nothing in an
unfinished block runs until the complete block is submitted.

The editor accepts printable ASCII and edits only the end of the line.
There is no cursor movement, command history, function-key interface or
full-screen editor. Arrow keys send escape sequences and can cancel input;
use Backspace instead. Terminal scrollback belongs to the computer.

Use `clear A; clear B;` to remove two names. `clear A B` is not supported.
Cancellation is checked during computation, so it may take a moment to return.
Already completed assignments are not automatically undone.

## 4. Numbers, variables and operators

Numbers use 64-bit floating point (`double`), giving about 15–16 significant
decimal digits. This is finite precision: use tolerances when comparing
calculated results. `1e-6` means one millionth. `pi`, `eps`, `Inf` and `NaN`
are available. There is no general complex-number arithmetic.

Names are case sensitive: `a` and `A` differ, and `Sin` is not `sin`.
Use letters followed by letters, digits or underscores, and avoid naming a
variable after a command or function you want to use. There are 16 variable
slots, including values such as `ans`.

| Operator | Meaning |
|---|---|
| `+`, `-` | Addition, subtraction; also unary signs. |
| `*`, `/` | Multiplication and division; matrix operations for matrix operands. |
| `\` | Solve a linear system: `A\b`. |
| `^` | Power; integer powers for square matrices. |
| `.*`, `./`, `.^` | Element-by-element multiplication, division and power. |
| `'` | Transpose. |
| `==`, `~=`, `<`, `<=`, `>`, `>=` | Comparisons, producing 1 or 0. |
| `&`, `|`, `~` | Element-by-element logical AND, OR and NOT. |
| `&&`, `||` | Short-circuit scalar AND and OR. |
| `%` | Start a comment outside a quoted string. |

Parentheses make grouping explicit. Powers group **from left to right**:
`2^3^2` is `(2^3)^2`, or 64. Power binds before a unary minus: `-2^2` is -4.
This is a MATLAB-like language with its own rules, not full MATLAB.

```mathsmith
:reset
2^3^2
-2^2
mod(-7,3)
rem(-7,3)
abs(sin(pi))<1e-12
```

```text
ans = 64
ans = -4
ans = 2
ans = -1
ans = 1
```

Elementary functions apply to each matrix element: `sqrt`, `abs`, `exp`,
`log` (natural logarithm), `log10`, `sin`, `cos`, `tan`, `asin`, `acos`, `atan`,
`floor`, `ceil`, `round`, `fix` and `sign`. `atan2(y,x)` accounts for the quadrant.
`mod(a,b)` takes the divisor's sign; `rem(a,b)` takes the dividend's sign.
Real-domain violations can produce `NaN`; they do not create complex values.

## 5. Vectors, matrices and indexing

Separate columns with spaces or commas and rows with semicolons. A matrix
may contain **at most 64 elements**, including strings and temporary matrices.
An 8×8 square fits this size rule; so does a 1×64 vector. Memory can impose a
smaller practical limit for a calculation using several intermediate matrices.

```mathsmith
:reset
v=1:2:9;
sum(v)
A=[1 2;3 4];
A(2,1)
A(2)
A(end)
A(1,2)=20;
sum(A(:))
```

```text
ans = 25
ans = 3
ans = 3
ans = 4
ans = 28
```

Indices start at **1**. `A(row,column)` selects an element; `A(:,2)` selects
the second column, `A(1,:)` the first row, and `A(:)` stacks all columns into
one column. A single index counts **down columns**: for `[1 2;3 4]` the order
is 1, 3, 2, 4. `end` means the last index in the relevant dimension.

`first:last` uses a step of 1; `first:step:last` uses the given step. For
example `5:-1:1` counts backwards. Ordinary range values must fit the element
limit. A numeric `for` range is iterated without storing the whole vector.

```mathsmith
:reset
A=[1 2;3 4];
B=A.*A;
B(2,2)
C=A*A;
C(2,2)
R=reshape(1:6,2,3);
R(2,2)
numel(linspace(0,1,5))
```

```text
ans = 16
ans = 22
ans = 4
ans = 5
```

| Function | Use |
|---|---|
| `zeros(n)`, `ones(n)`, `eye(n)`, `rand(n)` | Make an `n`×`n` matrix. |
| `zeros(r,c)`, `ones(r,c)`, `eye(r,c)`, `rand(r,c)` | Make a rectangular matrix. |
| `linspace(a,b,n)` | Make `n` equally spaced values, including both ends. |
| `size(A)`, `size(A,1)`, `size(A,2)` | Shape, row count, column count. |
| `length(A)`, `numel(A)` | Largest dimension, total element count. |
| `reshape(A,r,c)` | Change shape, preserving the column-wise ordering. |
| `diag(v)`, `diag(A)` | Build a diagonal matrix, or extract a matrix diagonal. |
| `transpose(A)`, `A'` | Exchange rows and columns. |
| `find(A)` | Linear indices of nonzero elements. |
| `any(A)`, `all(A)`, `isempty(A)` | Logical tests. |

**Always supply the count to `linspace`.** Its inherited two-argument default
is 100 points, which exceeds L00's limit. Similarly, `diag` of a 9-element
vector creates an oversized 9×9 result. `zeros(size(A))` is not supported;
write `zeros(size(A,1),size(A,2))`.

## 6. Solving systems and finding eigenvalues

Solve `4*x-2*y=2`, `-2*x+4*y=8` with left division:

```mathsmith
:reset
A=[4 -2;-2 4];
b=[2;8];
x=A\b;
x(1)
x(2)
norm(A*x-b)<1e-10
det(A)
```

```text
ans = 2
ans = 3
ans = 1
ans = 12
```

Use `A\b` rather than explicitly calculating an inverse to solve a system.
Check a result with its residual, such as `norm(A*x-b)`. Singular matrices or
poorly conditioned problems can fail or magnify rounding errors.

The following is the eigenvalue calculation already tried on the real board:

```mathsmith
:reset
A=[1 2 3 4;2 3 4 1;3 4 1 2;4 1 2 3];
eig(A)
```

```text
ans =
  -2.8284
  -2.0000
   2.8284
  10.0000
```

| Function | Result |
|---|---|
| `det(A)`, `trace(A)`, `rank(A)` | Determinant, diagonal sum, numerical rank. |
| `inv(A)`, `pinv(A)` | Inverse, pseudo-inverse. |
| `norm(v)`, `norm(A)` | Vector length, or matrix spectral norm. |
| `norm(A,'fro')` | Frobenius norm. |
| `cond(A)` | Condition number from singular values. |
| `eig(A)` | Real eigenvalues in ascending order. |
| `[V,D]=eig(A)` | Eigenvectors in columns and diagonal eigenvalue matrix. |
| `eigc(A)` | Eigenvalues as two real columns: real and imaginary parts. |
| `svd(A)` | Singular values, largest first. |
| `[U,S,V]=svd(A)` | Economy decomposition satisfying `A=U*S*V'`. |

`eig` refuses genuinely complex eigenvalues; use `eigc` to inspect their
real/imaginary parts. These pairs do not provide general complex arithmetic
or complex eigenvectors. Eigenvector signs can differ between programs.
For a defective nonsymmetric matrix, returned eigenvectors need not form an
independent basis. Multiple-output operations also require more memory.

## 7. Statistics and polynomials

```mathsmith
:reset
d=[2 4 6 8];
mean(d)
median(d)
sum(d)
var(d)
trapz([0 1 2],[0 1 4])
p=polyfit([0 1 2 3],[1 3 7 13],2);
abs(polyval(p,4)-21)<1e-10
```

```text
ans = 5
ans = 5
ans = 20
ans = 6.6667
ans = 3
ans = 1
```

For a vector, reductions work across the vector. For a matrix, most work
down each column. `sum(A,2)`, `mean(A,2)` and `prod(A,2)` operate along rows.
`sum(sum(A))` totals all elements. `std` and `var` use the sample divisor
`n-1`. `max(a,b)` and `min(a,b)` compare pairs of elements, allowing a scalar.

Other operations: `prod`, `max`, `min`, `sort`, `cumsum`, `cumprod`, `diff`,
`std`, `dot(a,b)`, `trapz(y)` and `trapz(x,y)`. `[s,k]=sort(v)` gives sorted
values and their original indices; `[m,k]=max(v)` or `min(v)` gives a value
and its first position. `sort`, `max`, `min`, `cumsum` and `cumprod` do not
accept a dimension selector.

Polynomial coefficients go **highest power first**: `[1 -3 2]` represents
`x^2-3*x+2`. `polyfit(x,y,n)` fits degree `n`; `polyval(p,x)` evaluates it.
`roots(p)` returns real roots; `rootsc(p)` returns real/imaginary pairs.
Choose modest degrees: internal work arrays must also fit the 64-element
and total-memory limits, even when the coefficient vector is small.

## 8. Functions and numerical tools

An anonymous function puts a short expression behind a name:

```mathsmith
:reset
f=@(x) x.^2+1;
f(3)
sum(f(1:3))
k=10;
g=@(x) x+k;
k=20;
g(1)
```

```text
ans = 10
ans = 17
ans = 21
```

Use `.*`, `./` and `.^` when a formula should work element by element.
Anonymous functions can have up to three parameters; four function definitions
fit this profile. The expression text is limited to 159 characters, within
the console's line limit. They see outside variables **when called**, so
changing `k` changes `g` in this example.

`@sin` is a handle to an existing function. Numerical tools take a function
that returns one number for a numeric scalar input:

```mathsmith
:reset
format long
fzero(@(x) x^2-2,[1 2])
integral(@(x) x^2,0,1)
deriv(@sin,0)
abs(fminbnd(@(x) (x-3)^2,0,5)-3)<1e-5
sum(arrayfun(@(x) x^2,1:3))
```

```text
ans = 1.41421356
ans = 0.33333333
ans = 1.00000000
ans = 1
ans = 14
```

| Function | Purpose |
|---|---|
| `fzero(f,[a b])` | Find a zero bracketed by opposite signs at the endpoints. |
| `fzero(f,x0)` | Search for a zero near a starting guess. |
| `integral(f,a,b)`, `quad(f,a,b)` | Numerically integrate over finite bounds. |
| `deriv(f,x)`, `deriv(f,x,2)` | Estimate the first or second derivative. |
| `fminbnd(f,a,b)` | Find a local minimum's position inside the interval. |
| `arrayfun(f,A)` | Apply a scalar function to each element. |

These are numerical approximations. Read convergence warnings; a root search
based on a sign change can miss a root that merely touches zero. L00 limits
numerical-tool function evaluations to 2000 per command. Keep formulas simple
and avoid deep function calls on this small target.

## 9. Writing short programs

### Repetition and decisions

Enter the following lines individually. The prompt changes to `..>` after
`for`, and returns to `>>` after its matching `end` runs the block:

```mathsmith
:reset
s=0;
for k=1:8
  s=s+k;
end
s
if s==36
  disp('sum is correct')
else
  disp('check the calculation')
end
```

```text
s = 36
sum is correct
```

`if` can include `elseif` and `else`. A condition is true when it is nonempty
and all its elements are nonzero. Use `==` to compare; `=` assigns.

```mathsmith
:reset
n=5;
p=1;
while n>1
  p=p*n;
  n=n-1;
end
p
```

```text
p = 120
```

`break` leaves a loop; `continue` skips to its next iteration. `return` stops
the current top-level program or returns from a function. Close every block with plain `end`.
Use Esc, Ctrl+C or `abort` if you started the wrong block. Loops share the
prompt's variables, so counters remain afterwards. There is no automatic
save, program-file loading or separate process for these programs.

### Named multiline functions

```mathsmith
:reset
function y=area(w,h)
  y=w*h;
end
area(3,4)
```

```text
ans = 12
```

The declared output variable must be assigned. These functions have their
own local variables and see their inputs rather than the prompt's variables.
Up to four named multiline functions are supported, with up to six input
parameters and one output each. Function-call depth is limited to four
(anonymous calls have their own depth bound). Memory and the stack reserve
can stop a calculation before a nominal limit is reached. Avoid recursion.

## 10. Text and timing

Use single or double quotes for strings. `disp(value)` prints without the
usual variable-name label. `fprintf` prints formatted output; `sprintf`
returns it as text. `num2str(x)` converts a value to text and `strcmp(a,b)`
compares strings. Strings also count toward the element limit.

```mathsmith
:reset
volts=3.3;
fprintf('Voltage: %.2f V\n',volts)
strcmp('ready','ready')
disp('Finished')
```

```text
Voltage: 3.30 V
ans = 1
Finished
```

Common formats are `%d` (integer), `%f` (decimal), `%e` (exponent), `%g`
(compact number) and `%s` (string). `%.3f` selects three decimals; `\n`
starts a new line. Keep messages short: formatting is bounded, and the
six-argument call limit includes the format string.

`error('message')` stops the running program with an error. `tic` starts
a timer and `toc` reports elapsed seconds. These timings include the work
done between the two commands; interactive pauses can therefore be included.

## 11. Memory and limits

Use `:mem` to inspect actual board diagnostics. The user reported these
values after entering the 4×4 matrix and calling `eig(A)`:

```text
Heap free: 27888; largest block: 27304; fragmentation: 3%
Engine payload: used=5072 peak=5664 limit=16384 bytes
Stack: allocated=16384 minimum free=11596 bytes
```

These are observations from that session, not guaranteed starting values.
The initial boot diagnostics run **before** the engine workspace is allocated,
so their `used=0` and higher free heap are not the cost of an idle ready prompt.

| Field | How to interpret it |
|---|---|
| Heap free | Total currently available SDK heap. |
| Largest block | Largest single allocation possible before overhead. Fragmentation can make this smaller than total free heap. |
| Engine used / peak / limit | Live, highest recorded and allowed engine payload bytes; allocator metadata is additional. |
| Stack minimum free | Smallest unused continuation-stack margin measured so far, rather than just current use. |
| Wi-Fi mode 0 | Wi-Fi is off, as intended for L00. |

The engine budget is a separate cap; having free SDK heap does not bypass it.
Matrix results, solver workspaces, stored blocks and functions all consume
resources. Some small scalar allocations are cached, so `clear` need not
return diagnostics to their boot values.

| Resource | L00 limit |
|---|---|
| Numeric storage | 64-bit `double`. |
| Variable slots | 16, including `ans`. |
| Elements in any one matrix | 64; an 8×8 square or another shape within that total. |
| Anonymous / named multiline functions | 4 of each kind. |
| Arguments to an ordinary builtin | 6. |
| Input | 191 printable ASCII characters per line. |
| Bracket nesting / consecutive unary operators | 8. |
| Stored block statements | 128, subject to memory. |
| Numerical-tool evaluations | 2000 per command. |
| Format / output buffer capacities | 128 / 256 bytes, including terminators; returned strings also face the element limit. |
| Engine payload | 16384 bytes. |
| Dedicated continuation stack | 16384 bytes, with parser reserve checks. |

After a resource error, try a simple `2+2`, inspect `:mem`, then free unused
names. Use `:reset` for a clean workspace if necessary. Reduce intermediate
results and work in smaller pieces. A size limit does not guarantee that every
combination at that size fits; error handling does not roll back a whole program.

## 12. Troubleshooting

| Symptom | What to do |
|---|---|
| Brief garbled text during reset, then a normal prompt | Boot ROM and firmware use different baud rates. Continue at 115200. |
| Repeated garbage, reboot banners or `Fatal exception` | Capture ROM output at 74880, then application output at 115200. Keep the exact firmware ELF for diagnosis; follow the L00 hardware instructions. |
| Port busy / cannot open | Close other monitors; confirm the device name and serial access permissions. |
| No characters or prompt | Check the USB data cable and port, use 115200 8N1 with flow control off, and press Reset. |
| Characters appear twice | Disable the terminal's local echo; firmware echoes input. |
| `..>` keeps appearing | A block needs its matching `end`. Use `abort` or Esc to discard it. |
| `Input too long; line discarded` | Re-enter a shorter line. Overflow discards the whole line. |
| `Expression nesting/unary limit is 8` | Simplify the expression using intermediate variables. |
| Too many variables | Inspect `who`; clear unused names, remembering that `ans` uses a slot. |
| Matrix larger than 64 elements | Reduce dimensions; give `linspace` an explicit count of at most 64. Check intermediate shapes. |
| Out of memory / stack reserve reached | Simplify the calculation, reduce function nesting, clear values, or use `:reset`. |
| Commands vanish during a large paste | Type at each prompt. Typeahead during calculations is bounded; overflow cancels and discards queued input. |
| `fft`, file commands or `:app` fail | These features are absent from L00. |
| `plot` produces no chart | There is no rendering frontend. Some inherited plot bookkeeping may accept commands without visible output. |
| `exit` does not close the console | It keeps MathSmith running. Ctrl+] exits the supplied monitor. |
| Lost work after reset or power-off | The workspace is RAM only. Keep program text and recorded results on the computer. |

In VS Code's terminal, the serial monitor still uses **Ctrl+]** to exit.
Ctrl+A is not an ESP8266 firmware exit key. If a terminal intercepts Ctrl+C,
try Esc for calculation cancellation. Never send another program's shortcut
sequence expecting the DOS/Linux MathSmith full-screen interface.

## 13. Firmware, peripherals and further reading

### Installing or rebuilding

**An already working board does not need reflashing to use this manual.**
From the repository root, the development workflow is:

```sh
python3 tools/setup_esp8266.py
esp8266/scripts/mathsmith.sh build
esp8266/scripts/mathsmith.sh test
esp8266/scripts/mathsmith.sh release
```

These commands leave the board untouched. Hardware backup, flashing and
serial-port commands are documented in the [L00 build and hardware guide](../../apps/esp8266/l00_mathsmith/README.md).
Back up any firmware you need before replacing it. The target is the identified
**ESP8266EX Wemos D1 mini, 4 MB flash, 26 MHz crystal**, using the pinned Arduino
ESP8266 core **3.1.2** at **80 MHz**. ESP32 and RP2350 images are separate builds.

`esp8266/scripts/mathsmith.sh native` runs the light engine on Linux; Ctrl+D
ends that harness. It is useful for checking programs, but is not an ESP8266
emulator and does not measure the board's heap, stack, power or timing.

The release folder `releases/esp8266/l00_mathsmith/` contains a copy of this
manual, the firmware candidate and checksums. To recheck its example sessions:

```sh
python3 esp8266/scripts/check_manual.py
```

### Planned physical console and AI

The user owns SSD1306 0.96-inch OLEDs, a PS/2 connector and a bidirectional
3.3 V/5 V level shifter. The four-pin display labelled Vcc/Gnd/SCL/SDA is I2C
and is the proposed first display. The seven-pin display is a separate SPI
option. Its controller label does not by itself verify its resolution.

**L00 does not use these peripherals.** The proposed I2C OLED and PS/2 hookup,
voltage considerations and staged tests are in
[OLED and PS/2 planning](OLED_PS2_WIRING.md). The initial proposed geometry is
128×64 and must be verified on the module. Do not connect 5 V PS/2 signals
directly to ESP8266 GPIO.

A later AI client would send requests to a computer running the model. L00
contains neither that network client nor an onboard language model. Wi-Fi,
display and keyboard must be budgeted and tested together before adding them.

Repository references:

- [ESP8266 platform overview](../README.md)
- [L00 build instructions and numbered hardware test](../../apps/esp8266/l00_mathsmith/README.md)
- [Memory map](MEMORY_MAP_L00.md)
- [Peripheral resource budget](PERIPHERAL_BUDGET.md)
- Recorded real-board boot and eigenvalue report (local hardware log omitted; see the validation summary)

Paths above are relative to the repository manual. In a standalone release
folder, use `README.md`, `MEMORY_MAP.md` and `HARDWARE_REPORT.txt` for the
bundled development and hardware references.
