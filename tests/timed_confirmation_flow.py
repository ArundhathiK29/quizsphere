"""Actual console/pipe regression: embedded CR must never become 'yes'."""
from contextlib import closing
import os
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile

exe = str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix="qs-confirmation-") as directory:
    db = str(Path(directory) / "confirmation.sqlite")
    env = dict(os.environ, QUIZSPHERE_DB=db, QUIZSPHERE_AUTH_LOG=str(Path(directory) / "auth.log"))
    def run(data):
        result = subprocess.run([exe], input=data, capture_output=True, env=env, cwd=directory, timeout=15)
        assert result.returncode == 0, result.stderr
        return result.stdout
    secret = "FixturePass123!"
    setup = (f"root_admin\n{secret}\n{secret}\n2\nteacher\ni\n{secret}\n{secret}\n"
             f"2\nstudent\ns\n{secret}\n{secret}\n1\nteacher\n{secret}\n"
             "1\n3\nq\nMath\nEasy\nTwo plus two?\n3|4\n1\n0\n"
             "2\nTimed\n5\n1\nMath\nEasy\n4\n0\n0\n0\n")
    run(setup.encode())
    with closing(sqlite3.connect(db)) as connection:
        quiz = connection.execute("SELECT id FROM quizzes").fetchone()[0]
    prefix = f"1\nstudent\n{secret}\n1\n{quiz}\ng\ns\n".encode()
    for malformed in (b"y\res\n", b"ye\rs\n", b"y\res\r\n", b"yes\x1b\n", b"yes\x00\n"):
        run(prefix + malformed + b"0\n0\n0\n")
        with closing(sqlite3.connect(db)) as connection:
            assert connection.execute("SELECT count(*) FROM reports").fetchone()[0] == 0, malformed
            assert connection.execute("SELECT submitted FROM attempts").fetchone()[0] == 0, malformed
    # Conventional CRLF line termination still accepts an exact affirmative.
    run(prefix + b"yes\r\n0\n0\n")
    with closing(sqlite3.connect(db)) as connection:
        assert connection.execute("SELECT count(*) FROM reports").fetchone()[0] == 1
        assert connection.execute("SELECT submitted FROM attempts").fetchone()[0] == 1
    print("PASS timed confirmation: embedded controls rejected, exact CRLF yes accepted")
