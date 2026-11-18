"""Real console Student/Instructor/Admin report flow against disposable SQLite."""
from contextlib import closing
import csv
import os
from pathlib import Path
import re
import sqlite3
import subprocess
import sys
import tempfile
import time

exe = str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix="qs-system-") as directory:
    root = Path(directory)
    db = root / "system.sqlite"
    env = dict(os.environ, QUIZSPHERE_DB=str(db), QUIZSPHERE_AUTH_LOG=str(root / "auth.log"))
    secret = "FixturePass123!"
    def run(commands):
        result = subprocess.run([exe], input=commands.encode(), capture_output=True, env=env, cwd=root, timeout=20)
        assert result.returncode == 0, result.stderr
        return result.stdout.decode(errors="replace")
    run(f"root_admin\n{secret}\n{secret}\n2\nteacher\ni\n{secret}\n{secret}\n"
        f"2\nstudent\ns\n{secret}\n{secret}\n1\nteacher\n{secret}\n1\n"
        "3\nq1\nMath\nEasy\n2+2?\n3|4\n1\n3\nq2\nScience\nEasy\n3*3?\n8|9\n1\n0\n"
        "2\nSystem quiz\n5\n2\n\n\n4\n1\n0\n0\n")
    with closing(sqlite3.connect(db)) as connection:
        quiz = connection.execute("SELECT id FROM quizzes").fetchone()[0]
        student_id = str(connection.execute("SELECT id FROM users WHERE username='student'").fetchone()[0])
        hashes = connection.execute("SELECT password_hash FROM users").fetchall()
        assert all(secret not in row[0] and len(row[0]) > 40 for row in hashes)
    output = root / "student.txt"
    with output.open("wb") as stream:
        process = subprocess.Popen([exe], stdin=subprocess.PIPE, stdout=stream, stderr=stream, env=env, cwd=root)
        try:
            process.stdin.write(f"1\nstudent\n{secret}\n1\n{quiz}\ng\n".encode())
            process.stdin.flush()
            until = time.monotonic() + 10
            while "n next | p previous" not in output.read_text(errors="replace"):
                assert process.poll() is None
                assert time.monotonic() < until
                time.sleep(.01)
            visible = output.read_text(errors="replace").rsplit("\nQuestion ", 1)[-1]
            correct = "4" if "2+2?" in visible else "9"
            option = re.search(r"(?m)^(\d+)\. " + correct + r"\s*$", visible).group(1)
            process.stdin.write(f"a {option}\ns\nyes\n0\n0\n".encode())
            process.stdin.flush()
            process.wait(timeout=15)
            assert process.returncode == 0
        finally:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=5)
            process.stdin.close()
    text = output.read_text(errors="replace")
    assert "Weakest" in text or "weakest" in text, text
    assert secret not in text
    with closing(sqlite3.connect(db)) as connection:
        assert connection.execute("SELECT score,max_score FROM reports").fetchone() == (4.0, 8.0)
    # Reopen the application for each role; verify persistence plus actual CSV contents.
    for username in ("teacher", "root_admin"):
        target = root / f"{username}.csv"
        text = run(f"1\n{username}\n{secret}\n3\n2\n{quiz}\n{student_id}\n"
                   f"3\n{student_id}\n{target}\n0\n0\n0\n")
        assert "Topic analytics" in text and "Exported to" in text, text
        with target.open(newline="") as file:
            rows = list(csv.DictReader(file))
        assert len(rows) == 1 and float(rows[0]["score"]) == 4 and float(rows[0]["max_score"]) == 8, rows
    text = run(f"1\nstudent\n{secret}\n2\n{quiz}\n3\n1\n{quiz}\n2\nMath\n3\n0\n0\n0\n")
    assert "Leaderboard:" in text and "All-time leaderboard" in text
    assert secret not in text
    print("PASS Student grading/itemised+weak-topic summary; restart; all rankings; Instructor/Admin topic analytics and CSV transcripts; secret storage/output checks")
