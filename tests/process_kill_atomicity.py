"""UT-DAL-01: kill the actual app during its real grading transaction."""
from contextlib import closing
import os
from pathlib import Path
import shutil
import sqlite3
import subprocess
import sys
import tempfile
import time

exe = str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix="qs-atomicity-") as directory:
    root = Path(directory)
    seed = root / "seed.sqlite"
    secret = "FixturePass123!"
    def environment(db):
        return dict(os.environ, QUIZSPHERE_DB=str(db), QUIZSPHERE_AUTH_LOG=str(root / "auth.log"))
    setup = (f"root_admin\n{secret}\n{secret}\n2\nteacher\ni\n{secret}\n{secret}\n"
             f"2\nstudent\ns\n{secret}\n{secret}\n1\nteacher\n{secret}\n"
             "1\n3\nq\nMath\nEasy\nTwo plus two?\n3|4\n1\n0\n"
             "2\nAtomic\n5\n1\nMath\nEasy\n4\n0\n0\n0\n")
    result = subprocess.run([exe], input=setup.encode(), capture_output=True, cwd=root, env=environment(seed), timeout=15)
    assert result.returncode == 0, result.stderr
    with closing(sqlite3.connect(seed)) as connection:
        quiz = connection.execute("SELECT id FROM quizzes").fetchone()[0]
        # Test-only trigger pauses INSERT reports after the attempt/answer writes.
        # It uses SQLite's own recursive query, not a replacement implementation.
        connection.execute("""CREATE TRIGGER delay_report BEFORE INSERT ON reports BEGIN
          SELECT (WITH RECURSIVE counter(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM counter
          WHERE x<100000000) SELECT sum(x) FROM counter); END""")
        connection.commit()
    for run in range(3):
        db = root / f"kill-{run}.sqlite"
        shutil.copy2(seed, db)
        output = root / f"console-{run}.txt"
        with output.open("wb") as stream:
            process = subprocess.Popen([exe], stdin=subprocess.PIPE, stdout=stream, stderr=stream,
                                       cwd=root, env=environment(db))
            try:
                process.stdin.write(f"1\nstudent\n{secret}\n1\n{quiz}\ng\ns\n".encode())
                process.stdin.flush()
                deadline = time.monotonic() + 10
                while b"Submit this attempt?" not in output.read_bytes():
                    assert process.poll() is None, output.read_text(errors="replace")
                    assert time.monotonic() < deadline, "Confirmation prompt did not appear"
                    time.sleep(.01)
                process.stdin.write(b"yes\n")
                process.stdin.flush()
                journal = Path(str(db) + "-journal")
                while not journal.exists() or journal.stat().st_size == 0:
                    assert process.poll() is None, "Save completed before the interruption point"
                    assert time.monotonic() < deadline, "Transaction journal did not appear"
                    time.sleep(.005)
                time.sleep(.1)
                assert process.poll() is None, "No active transaction to interrupt"
                process.kill()
                process.wait(timeout=5)
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait(timeout=5)
                process.stdin.close()
        with closing(sqlite3.connect(db)) as connection:
            assert connection.execute("PRAGMA integrity_check").fetchone()[0] == "ok"
            assert connection.execute("SELECT submitted FROM attempts").fetchone()[0] == 0
            assert connection.execute("SELECT count(*) FROM reports").fetchone()[0] == 0
            assert connection.execute("SELECT count(*) FROM items").fetchone()[0] == 0
        print(f"PASS UT-DAL-01 trial {run+1}: killed real app mid-grading; complete rollback; integrity ok")
