"""Exercise real HTTP reads and verify persisted expiry without another API request."""
from contextlib import closing
import http.client
import json
from pathlib import Path
import socket
import sqlite3
import subprocess
import sys
import tempfile
import time
import urllib.parse

exe = str(Path(sys.argv[1]).resolve())
case = sys.argv[2]
with tempfile.TemporaryDirectory(prefix="qs-web-timer-") as directory:
    db = str(Path(directory) / "timer.sqlite")
    with closing(socket.socket()) as available:
        available.bind(("127.0.0.1", 0))
        port = available.getsockname()[1]
    host = f"127.0.0.1:{port}"
    def call(route, data=None, identity=None):
        headers = {}
        if identity:
            headers.update(Cookie=identity[0], **{"X-CSRF-Token": identity[1]})
        if data is not None:
            headers.update(Origin="http://" + host, **{"Content-Type": "application/x-www-form-urlencoded"})
        connection = http.client.HTTPConnection("127.0.0.1", port, timeout=4)
        try:
            connection.request("GET" if data is None else "POST", route,
                               None if data is None else urllib.parse.urlencode(data), headers)
            result = connection.getresponse()
            raw = result.read()
            return result.status, json.loads(raw) if route.startswith("/api/") else raw, result.getheader("Set-Cookie")
        finally:
            connection.close()
    def ok(route, data=None, identity=None):
        status, result, cookie = call(route, data, identity)
        assert status == 200, (status, result)
        return result, cookie
    def login(name):
        result, cookie = ok("/api/login", {"username": name, "password": "DemoPass123!"})
        return cookie, result["csrf"]
    def start():
        process = subprocess.Popen([exe, "--port", str(port), "--db", db], stdout=subprocess.DEVNULL,
                                   stderr=subprocess.PIPE)
        last = None
        for _ in range(80):
            try:
                last = call("/")
                if last[0] == 200:
                    return process
            except OSError as error:
                last = repr(error)
            assert process.poll() is None, process.stderr.read()
            time.sleep(.05)
        process.terminate()
        process.wait(timeout=5)
        raise AssertionError(("Server startup timed out", last))
    def persisted():
        with closing(sqlite3.connect(db, timeout=3)) as connection:
            row = connection.execute("SELECT submitted,auto FROM attempts WHERE quiz=?", (quiz,)).fetchone()
            reports = connection.execute("SELECT count(*) FROM reports").fetchone()[0]
            return row, reports
    process = start()
    slow = None
    try:
        for name, role in (("teacher", "Instructor"), ("student", "Student")):
            ok("/api/register", {"username": name, "password": "DemoPass123!", "role": role})
        teacher, student = login("teacher"), login("student")
        ok("/api/questions/save", {"id": "q", "subject": "Math", "difficulty": "Easy", "text": "2+2?",
                                   "options": "3\n4", "correct": 1, "available": "true"}, teacher)
        created, _ = ok("/api/quizzes/create", {"title": "Timer", "minutes": 1, "count": 1,
                                              "marks": 4, "penalty": 0}, teacher)
        quiz = created["id"]
        ok("/api/attempt/start", {"quiz": quiz, "mode": "graded"}, student)
        process.terminate()
        process.wait(timeout=5)
        with closing(sqlite3.connect(db)) as connection:
            if case != "cross-session-manual":
                connection.execute("UPDATE attempts SET started=strftime('%Y-%m-%dT%H:%M:%SZ','now','-58 seconds')")
            connection.commit()
        process = start()
        student = login("student")
        state, _ = ok("/api/attempt/start", {"quiz": quiz, "mode": "graded"}, student)
        assert not state["completed"], "Fixture expired before the test started"
        if case.startswith("cross-session-"):
            second = login("student")
            ok("/api/attempt/start", {"quiz": quiz, "mode": "graded"}, second)
            if case == "cross-session-manual":
                ok("/api/attempt/action", {"action": "submit", "confirmed": "true"}, student)
            else:
                time.sleep(3)
            # Another session's completion must be reconciled before polling/actions/guarding.
            state, _ = ok("/api/attempt", identity=second)
            assert state.get("completed"), ("Other session retained an open attempt", state)
            assert call("/api/attempt/action", {"action": "answer", "index": 0}, second)[0] == 409
            state, _ = ok("/api/attempt/start", {"quiz": quiz, "mode": "practice"}, second)
            assert state["practice"]
            # Login again because the fixture restart invalidated the original teacher cookie.
            teacher = login("teacher")
            new_quiz, _ = ok("/api/quizzes/create", {"title": "Next", "minutes": 1, "count": 1,
                                                    "marks": 4, "penalty": 0}, teacher)
            state, _ = ok("/api/attempt/start", {"quiz": new_quiz["id"], "mode": "graded"}, second)
            assert not state["completed"] and not state["practice"]
            with closing(sqlite3.connect(db)) as connection:
                assert connection.execute("SELECT count(*) FROM reports").fetchone()[0] == 1
            print("PASS", case, ": shared completion reconciled; practice/next quiz allowed; no regrading")
        elif case == "practice-switch":
            status, result, _ = call("/api/attempt/start", {"quiz": quiz, "mode": "practice"}, student)
            assert status == 409, ("Open graded attempt was replaced by practice", status, result)
            time.sleep(3)
            assert persisted() == ((1, 1), 1), persisted()
            # Once finalization succeeds, the guard must stop blocking practice.
            state, _ = ok("/api/attempt/start", {"quiz": quiz, "mode": "practice"}, student)
            assert state["practice"] and not state["completed"]
        else:
            slow = socket.create_connection(("127.0.0.1", port), timeout=3)
            if case == "slow-header":
                initial = f"GET / HTTP/1.1\r\nHost: {host}\r\nX-Slow: "
            elif case == "slow-body":
                initial = (f"POST /api/login HTTP/1.1\r\nHost: {host}\r\nOrigin: http://{host}\r\n"
                           "Content-Type: application/x-www-form-urlencoded\r\nContent-Length: 65536\r\n\r\nusername=")
            else:
                raise AssertionError("Unknown case")
            slow.sendall(initial.encode())
            began = time.monotonic()
            closed = False
            while time.monotonic() - began < 3.5:
                try:
                    slow.sendall(b"x")
                except OSError:
                    closed = True
                    break
                time.sleep(.05)  # Frequent chunks must not extend the absolute deadline.
            # Read SQLite before finishing/closing the hostile request or making an API call.
            assert persisted() == ((1, 1), 1), ("Expiry stalled during " + case, persisted())
            assert closed, "Slow request exceeded its absolute deadline"
        print("PASS", case, ": exact deadline monitoring and persisted auto-submission")
    finally:
        if slow is not None:
            slow.close()
        process.terminate()
        process.wait(timeout=5)
