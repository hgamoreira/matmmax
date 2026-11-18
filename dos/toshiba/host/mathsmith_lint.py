"""A checker for MathSmith scripts (decision XD87): finds, by line, the mistakes that MATLAB habits cause and that MathSmith reports as errors.

The assistant's `fix` and `change` modes (host/writer.py) put its findings in the prompt: a 14-billion-parameter model fixes the error it is told
about and leaves the other lines of the same kind, so what can be known for certain is told to it. Every rule here is a thing the engine really
rejects (apps/m01_mathsmith/tests/test_guide.py runs each rule's example through the engine). The checker reads text only; it never runs anything.
"""
import re

MAX_FINDINGS = 12

# the most arguments these built-in functions take (checked against the engine); MATLAB's dimension, 'descend' and digits arguments are not there
MOST_ARGUMENTS = {"round": 1, "num2str": 1, "sort": 1, "cumsum": 1, "cumprod": 1, "find": 1, "max": 2, "min": 2}
# what to write instead of the commonest things that MATLAB has and MathSmith has not (each is shown with the finding)
INSTEAD = {
    "isnan": "x ~= x is 1 where x is NaN", "isinf": "abs(x) == Inf", "strcat": "join strings with [a, b]", "legend": "leave it out, or put the names in the title",
    "unique": "sort the values, then keep those that differ from the one before them (a loop)", "fliplr": "v(end:-1:1)", "flipud": "v(end:-1:1)",
    "mat2str": "num2str or sprintf", "int2str": "sprintf('%d', x)", "double": "a string already is its character codes", "char": "a string is a row of character codes",
    "repmat": "ones(r, c) * value, or a loop", "input": "put the value in the script", "figure": "leave it out", "close": "leave it out", "pause": "leave it out",
    "warning": "disp('...') or error('...')", "log2": "log(x) / log(2)", "factorial": "prod(1:n)", "hypot": "sqrt(a.^2 + b.^2)", "sinh": "(exp(x) - exp(-x)) / 2",
    "cosh": "(exp(x) + exp(-x)) / 2", "tanh": "(exp(x) - exp(-x)) ./ (exp(x) + exp(-x))", "randi": "floor(rand * n) + 1", "randn": "no normal random numbers: use rand",
    "isprime": "a loop with mod", "gcd": "a loop with mod", "upper": "no text functions: leave it out", "lower": "no text functions: leave it out",
    "subplot": "plot one thing at a time", "bar": "plot(x, y)", "hist": "no histograms: count with sum(x >= a & x < b)", "scatter": "plot(x, y, 'o')",
    "semilogy": "plot(x, log10(y))", "fplot": "plot(x, f(x)) over a linspace", "ezplot": "plot(x, f(x)) over a linspace", "xor": "(a | b) & ~(a & b)",
}
SIZE_MAKERS = ("zeros", "ones", "rand", "eye")
OPENING = {"if": 1, "for": 1, "while": 1, "function": 1, "end": -1}


def blanked(script):
    """([the lines with comments and strings made of spaces], [the lines where a # starts a comment]); the line structure is kept."""
    out, hashes = [], []
    for number, line in enumerate(script.split("\n"), 1):
        result, i, previous = [], 0, ""
        while i < len(line):
            c = line[i]
            if c == "%" or c == "#":
                if c == "#":
                    hashes.append(number)
                result.append(" " * (len(line) - i))
                break
            if c == '"' or (c == "'" and not (previous and (previous.isalnum() or previous in "_)]}'."))):
                j = i + 1
                while j < len(line):
                    if line[j] == c:
                        if j + 1 < len(line) and line[j + 1] == c:                 # a doubled quote is a quote inside the string
                            j += 2
                            continue
                        break
                    j += 1
                result.append(c + " " * max(j - i - 1, 0) + (c if j < len(line) else ""))
                i, previous = j + 1, c
                continue
            result.append(c)
            if not c.isspace():
                previous = c
            i += 1
        out.append("".join(result))
    return out, hashes


def statements(lines):
    """[(line number, text)] of the statements: a line is split at ; and , outside brackets."""
    found = []
    for number, line in enumerate(lines, 1):
        depth, start = 0, 0
        for i, c in enumerate(line + ";"):
            if c in "([{":
                depth += 1
            elif c in ")]}":
                depth -= 1
            elif c in ";," and depth <= 0 and i >= start:
                text = line[start:i].strip()
                if text:
                    found.append((number, text))
                start = i + 1
                depth = 0
    return found


def call_arguments(line, open_at):
    """The arguments of the call whose '(' is at open_at (on one line), or None if the call is not closed on the line."""
    depth, args, start = 0, [], open_at + 1
    for i in range(open_at, len(line)):
        c = line[i]
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
            if depth == 0:
                args.append(line[start:i].strip())
                return args
        elif c == "," and depth == 1:
            args.append(line[start:i].strip())
            start = i + 1
    return None


def matching_open(line, close_at):
    depth = 0
    for i in range(close_at, -1, -1):
        if line[i] in ")]}":
            depth += 1
        elif line[i] in "([{":
            depth -= 1
            if depth == 0:
                return i
    return -1


def check(script, missing=()):
    """[(line, message)] for the script, in line order, at most MAX_FINDINGS. `missing` is the names that MATLAB has and MathSmith has not."""
    lines, hashes = blanked(script)
    found = []

    def add(number, message):
        if (number, message) not in found:
            found.append((number, message))

    text = "\n".join(lines)
    assigned = set(re.findall(r"\b([A-Za-z_]\w*)\s*(?:\([^()=]*\))?\s*=(?!=)", text))
    defined = {}
    for number, line in enumerate(lines, 1):
        m = re.match(r"\s*function\b\s*(?:\[[^\]]*\]\s*=\s*|(\w+)\s*=\s*)?(\w+)", line)
        if m:
            defined.setdefault(m.group(2), number)
            if re.match(r"\s*function\s*\[", line):
                add(number, "a function has ONE output (function y = name(args)); [a, b] = is not possible")
    parameters = set()
    for line in lines:
        if re.match(r"\s*function\b", line):
            parameters.update(re.findall(r"\w+", line))

    for number in hashes:
        add(number, "# does not start a comment: use %")
    for number, line in enumerate(lines, 1):
        for m in re.finditer(r"\bprintf\s*\(", line):
            if "printf" not in assigned and "printf" not in defined:
                add(number, "printf does not exist: use fprintf('...\\n', values) (format first)")
        for m in re.finditer(r"\b(true|false)\b", line):
            if m.group(1) not in assigned and m.group(1) not in parameters:
                add(number, "%s does not exist: write 1 or 0" % m.group(1))
        for m in re.finditer(r"\bend(if|for|while|function|switch)\b", line):
            add(number, "end%s does not exist: close the block with end" % m.group(1))
        if "!" in line:
            add(number, "! and != do not exist: write ~ and ~=")
        if re.search(r"\+\+|\+=|-=|\*=|/=|\^=", line):
            add(number, "++ += -= *= /= ^= do not exist: write x = x + 1")
        if "{" in line or "}" in line:
            add(number, "cell arrays {} do not exist")
        if re.search(r"\b[A-Za-z_]\w*\.[A-Za-z_]", line):
            add(number, "structs (a.b) do not exist")
        if "..." in line:
            add(number, "a line cannot be continued with ...")
        for m in re.finditer(r"[)\]]\(", line):
            at = matching_open(line, m.start())
            if at < 0 or line[:at].rstrip().endswith("@"):          # @(x)(x + 1): an anonymous function with a bracketed body
                continue
            add(number, "only a variable can be indexed: put the value in a variable first, then index the variable")
        for m in re.finditer(r"\b(%s)\s*\(\s*size\s*\(([^()]*)\)\s*\)" % "|".join(SIZE_MAKERS), line):
            if "," not in m.group(2):
                add(number, "%s(size(A)) is an error: sizes are plain numbers, write %s(size(A,1), size(A,2))" % (m.group(1), m.group(1)))
        for m in re.finditer(r"\b(%s)\s*\(" % "|".join(MOST_ARGUMENTS), line):
            args = call_arguments(line, m.end() - 1)
            if args is not None and len(args) > MOST_ARGUMENTS[m.group(1)]:
                add(number, "%s takes at most %d argument%s here (no dimension, 'descend' or digits argument)"
                    % (m.group(1), MOST_ARGUMENTS[m.group(1)], "" if MOST_ARGUMENTS[m.group(1)] == 1 else "s"))
        for m in re.finditer(r"\[([^\]=]*,[^\]=]*)\]\s*=\s*(\w+)\s*\(", line):
            if m.group(2) in defined:
                add(number, "%s has one output: [a, b] = %s(...) is not possible; return a vector (y = [a; b]) and take y(1), y(2)" % (m.group(2), m.group(2)))
        stripped = line.strip()
        header = re.match(r"(?:if|elseif|while|for)\b(.*)", stripped)
        if header:                                                  # the condition ends at the first comma or semicolon outside brackets
            head = header.group(1)
            while True:
                shorter = re.sub(r"\([^()]*\)|\[[^\[\]]*\]", lambda m: "_" * len(m.group(0)), head)
                if shorter == head:
                    break
                head = shorter
            head = re.split(r"[,;]", head)[0]
            if re.search(r"[\w_'\"]\s+[A-Za-z_]", head.strip()):      # two operands side by side: a statement follows the condition
                add(number, "a block on one line needs a comma after its header: if x > 3, disp(x), end")
        for name in missing:
            if name != "printf" and name not in assigned and name not in defined and name not in parameters and re.search(r"\b%s\b" % re.escape(name), line):
                add(number, "%s does not exist in MathSmith%s" % (name, ": instead use " + INSTEAD[name] if name in INSTEAD else " (see the list of built-in functions)"))

    # a file that begins with function holds functions only; a function used before its definition in a script
    depth, first, trailing = 0, True, None
    function_file = False
    for number, statement in statements(lines):
        word = re.match(r"[A-Za-z_]\w*", statement)
        word = word.group(0) if word else ""
        if first:
            function_file, first = word == "function", False
        if depth == 0 and function_file and word != "function" and trailing is None:
            trailing = number
        depth += OPENING.get(word, 0)
        depth = max(depth, 0)
    if trailing is not None:
        add(trailing, "this file begins with function, so it is a function file: statements after the functions never run as a script. "
                      "Put a line  1;  above the first function (and define each function before the line that calls it)")
    elif defined:
        for name, at in defined.items():
            for number, line in enumerate(lines[:at - 1], 1):
                if re.search(r"(?<![\w.])%s\s*\(|@%s\b" % (name, name), line):
                    add(number, "%s is used before its definition on line %d: in a script a function must be defined above the line that calls it" % (name, at))
                    break
    return sorted(found)[:MAX_FINDINGS]


def explain_error(message, script):
    """Certain facts about the error MathSmith printed, worked out here so that the model does not have to guess them (XD90): the line it is on, and for a
    matrix size error which transposition or operator fits. '' when there is nothing to add."""
    notes = []
    m = re.search(r"\(line (\d+) of [^)]*\)", message)
    lines = script.split("\n")
    if m and 1 <= int(m.group(1)) <= len(lines):
        notes.append("The error is on line %s of the script: %s" % (m.group(1), lines[int(m.group(1)) - 1].strip()))
    m = re.search(r"inner matrix dimensions must agree \((\d+)x(\d+) times (\d+)x(\d+)\)", message)
    if m:
        r1, c1, r2, c2 = (int(g) for g in m.groups())
        text = ("In  A * B  (the matrix product) the number of columns of A must equal the number of rows of B. Here A is %dx%d and B is %dx%d, so it cannot work. "
                % (r1, c1, r2, c2))
        if (r1, c1) == (r2, c2):
            text += "Both have the same shape, so element-by-element multiplication is almost certainly meant: write  A .* B  (and .^ for powers, ./ for division). "
            if r1 == 1 or c1 == 1:
                text += "If a dot product of the two vectors is meant, write  sum(A .* B)."
        else:
            if c1 == c2:
                text += "A * B'  fits (B' is %dx%d). " % (c2, r2)
            if r1 == r2:
                text += "A' * B  fits (A' is %dx%d). " % (c1, r1)
            if r1 == r2 and c1 == c2:
                pass
            if (r1, c1) != (r2, c2) and not (c1 == c2 or r1 == r2):
                text += "No transposition fits: check how A and B were made. "
            text += ("Do NOT make the error go away by swapping the operands or adding a transpose unless the maths stays the same (A*B is not B*A): "
                     "work out what the result should be and which earlier line builds an operand wrongly. ")
            if (r1, c1) == (c2, r2):
                text += "B is the transpose of A's shape, so  A * B'  is wrong here but A .* B' (element-wise) may be what was meant. "
        notes.append(text.strip())
    if re.search(r"dimensions must agree|dimensions are not consistent", message) and re.search(r"\bsvd\s*\(", script):
        notes.append("The script uses svd. In MathSmith [U,S,V] = svd(A) is the economy one: for an m x n matrix A, U is m x n, S is n x n and V is n x n. "
                     "The pseudo-inverse of A is  V * inv(S) * U'  (n x m), the same as pinv(A); V * inv(S)' alone is only n x n. "
                     "Least squares:  coeff = V * inv(S) * U' * y   or  coeff = pinv(A) * y   or  coeff = A \\ y.")
    m = re.search(r"matrix dimensions must agree \((\d+)x(\d+) and (\d+)x(\d+)\)", message)
    if m:
        r1, c1, r2, c2 = (int(g) for g in m.groups())
        text = ("An element-wise operation (+ - .* ./ .^ and comparisons) needs two matrices of the same size, or one of them a single number. Here the sizes are %dx%d and %dx%d. "
                % (r1, c1, r2, c2))
        if (r1, c1) == (c2, r2):
            text += "One is the transpose of the other: transpose one of them with '. "
        elif r1 == 1 and c2 == 1 or c1 == 1 and r2 == 1:
            text += "A row and a column: make both rows or both columns with '. "
        notes.append(text.strip())
    m = re.search(r"horizontal dimensions are not consistent \((\d+) rows and (\d+) rows\)", message)
    if m:
        notes.append("Items joined side by side in [ a, b ] must have the same number of rows (here %s and %s). In a line such as  disp(['text ', num2str(v)])  the cause is "
                     "almost always that v is a column: num2str of a column is a column of text. Write num2str(v') so that it is a row." % (m.group(1), m.group(2)))
    m = re.search(r"vertical dimensions are not consistent \((\d+) columns and (\d+) columns\)", message)
    if m:
        notes.append("Rows joined with ; in [ a; b ] must have the same number of columns (here %s and %s)." % (m.group(1), m.group(2)))
    m = re.search(r"index (\d+) exceeds the matrix dimension (\d+)", message)
    if m:
        notes.append("An index may not be larger than the size of the matrix (here %s > %s). Check the loop limits and the sizes; indices start at 1." % (m.group(1), m.group(2)))
    m = re.search(r"undefined variable or function '([^']+)'", message)
    if m:
        name = m.group(1)
        first = next((n for n, line in enumerate(lines, 1) if re.search(r"\b%s\b" % re.escape(name), line)), None)
        notes.append("'%s' is not defined when it is used: it is either a typing mistake, something MathSmith does not have, or a variable that is assigned only later%s."
                     % (name, " (it first appears on line %d)" % first if first else ""))
    return "\n".join(notes)
