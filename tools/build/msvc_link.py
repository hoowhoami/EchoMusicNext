"""Keep MSVC response-file lines bounded without reinterpreting linker arguments."""

import codecs
import locale
import subprocess
import sys
import tempfile
from contextlib import ExitStack
from pathlib import Path


def normalize(data: bytes, limit: int = 8000) -> bytes:
    # Decode the writer's encoding before interpreting quotes: in a Windows
    # DBCS code page a trailing byte of a filename can also be 0x5c (backslash).
    if data.startswith((codecs.BOM_UTF16_LE, codecs.BOM_UTF16_BE)):
        encoding = "utf-16-le" if data.startswith(codecs.BOM_UTF16_LE) else "utf-16-be"
        prefix, payload = data[:2], data[2:]
    else:
        encoding, prefix, payload = "utf-8", b"", data
    try:
        text = payload.decode(encoding)
    except UnicodeDecodeError:
        # UTF-8 mode can override getpreferredencoding() without changing the
        # ANSI code page used by Ninja's response-file writer.
        encoding = locale.getencoding()
        try:
            text = payload.decode(encoding)
        except UnicodeDecodeError:
            encoding, text = "latin-1", payload.decode("latin-1")
    lines = []
    # splitlines() also treats Unicode filename characters such as NEL as line
    # separators, which would reset quote state in the middle of an argument.
    for line in text.split("\n"):
        if len(line.encode(encoding)) <= limit:
            lines.append(line)
            continue
        quoted = False
        backslashes = 0
        output = []
        for character in line:
            if character == '"' and backslashes % 2 == 0:
                quoted = not quoted
            # Preserve every non-delimiter byte, including Windows backslashes
            # and quotes inside /LIBPATH:"..." or a caller-provided launcher.
            output.append("\n" if character in " \t" and not quoted else character)
            backslashes = backslashes + 1 if character == "\\" else 0
        lines.append("".join(output))
    return prefix + "\n".join(lines).encode(encoding)


def run(command: list[str]) -> int:
    # Never overwrite Ninja's response file. Each invocation owns its temporary
    # copies until the complete vs_link_exe/link.exe command has returned.
    with ExitStack() as resources:
        arguments = []
        for argument in command:
            if argument.startswith("@"):
                source = Path(argument[1:])
                original = source.read_bytes()
                normalized = normalize(original)
                if normalized != original:
                    directory = resources.enter_context(
                        tempfile.TemporaryDirectory(prefix="echo-link-", dir=source.parent)
                    )
                    response = Path(directory) / source.name
                    response.write_bytes(normalized)
                    argument = "@" + str(response)
            arguments.append(argument)
        return subprocess.call(arguments)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit("Usage: msvc_link.py <link command> [arguments...]")
    sys.exit(run(sys.argv[1:]))
