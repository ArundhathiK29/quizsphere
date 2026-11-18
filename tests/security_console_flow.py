"""ST-SEC-08: run the real console and inspect its disposable SQLite database."""
import os
from contextlib import closing
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile

app = str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix="quizsphere-security-") as directory:
    database = str(Path(directory) / "security.sqlite")
    env = dict(os.environ, QUIZSPHERE_DB=database, QUIZSPHERE_AUTH_LOG=str(Path(directory) / "auth.log"))
    def run(commands):
        result = subprocess.run([app], input=commands.encode(), capture_output=True, env=env,
                                cwd=directory, timeout=20)
        assert result.returncode == 0, result.stderr.decode(errors="replace")
        return result.stdout.decode(errors="replace")
    secret = "FixturePass123!"
    run(f"root_admin\n{secret}\n{secret}\n0\n")
    for role in ("", "invalid", "instructorXYZ", "admin", "s "):
        output = run(f"2\nbad_user\n{role}\n{secret}\n{secret}\n0\n")
        assert "Invalid role" in output
    for username in ("", "x" * 33, "bad'user", "bad\x1buser", "x" * 100000):
        run(f"2\n{username}\ns\n{secret}\n{secret}\n0\n")
    run(f"2\nteacher\ni\n{secret}\n{secret}\n2\nstudent\ns\n{secret}\n{secret}\n0\n")
    with closing(sqlite3.connect(database)) as connection:
        names = {row[0] for row in connection.execute("SELECT username FROM users")}
        assert names == {"root_admin", "teacher", "student"}, names
    # Question and quiz creation must reject empty, oversized and malformed fields.
    login = f"1\nteacher\n{secret}\n"
    for command in ("1\n3\n\nMath\nEasy\nText\nA|B\n0\n0\n0\n0\n",
                    "2\n\n0\n0\n", "2\nQuiz\n1e3\n0\n0\n",
                    "2\n" + "x" * 100000 + "\n0\n0\n",
                    "3\n" + "x" * 100000 + "\n0\n0\n",
                    "3\n3\nstudent\nbad\x00.csv\n0\n0\n0\n"):
        run(login + command)
    # A malformed top-level Student choice cannot select a hidden management action.
    output = run(f"1\nstudent\n{secret}\n2suffix\n5\n0\n0\n")
    assert "Manage users ===" not in output
    output = run(f"1\nroot_admin\n{secret}\n2\n2\n" + "x" * 100000 + "\n0\n0\n")
    assert "Invalid input" in output
    with closing(sqlite3.connect(database)) as connection:
        assert connection.execute("SELECT count(*) FROM quizzes").fetchone()[0] == 0
        assert connection.execute("SELECT count(*) FROM questions").fetchone()[0] == 0
        assert connection.execute("SELECT count(*) FROM users").fetchone()[0] == 3
    print("PASS ST-SEC-08: auth, role menus, question/quiz creation, analytics/export and Admin negatives; storage unchanged.")
