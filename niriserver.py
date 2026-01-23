from http.server import BaseHTTPRequestHandler, HTTPServer
import json
import sqlite3
import time
import os

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
DB_PATH = os.path.join(BASE_DIR, "focus.db")
HOST = "127.0.0.1"
PORT = 8079

def init_db():
    conn = sqlite3.connect(DB_PATH)
    cur = conn.cursor()
    cur.execute("""
        CREATE TABLE IF NOT EXISTS focus_events (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            app_id TEXT,
            window_id INTEGER,
            title TEXT,
            start_time REAL,
            end_time REAL,
            duration REAL
        )
    """)
    cur.execute("""
        CREATE TABLE IF NOT EXISTS focus_tasks (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            category TEXT,
            task TEXT,
            start_time REAL,
            end_time REAL,
            allowed_app_ids TEXT,
            allowed_titles TEXT,
            excluded INTEGER DEFAULT 0,
            done INTEGER DEFAULT 0
        )
    """)
    try:
        cur.execute("ALTER TABLE focus_tasks ADD COLUMN excluded INTEGER DEFAULT 0")
    except Exception:
        pass
    try:
        cur.execute("ALTER TABLE focus_tasks ADD COLUMN done INTEGER DEFAULT 0")
    except Exception:
        pass
    conn.commit()
    conn.close()

current_focus = {}

class Handler(BaseHTTPRequestHandler):
    def do_POST(self):
        length = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(length)

        try:
            data = json.loads(body)
            if self.path == "/event":
                conn = sqlite3.connect(DB_PATH)
                cur = conn.cursor()
                cur.execute("""
                    INSERT INTO focus_events
                    (app_id, window_id, title, start_time, end_time, duration)
                    VALUES (?, ?, ?, ?, ?, ?)
                """, (
                    data.get("app_id", ""),
                    int(data.get("window_id", -1)),
                    data.get("title", ""),
                    float(data.get("start_time", 0.0)),
                    float(data.get("end_time", 0.0)),
                    float(data.get("duration", 0.0))
                ))
                conn.commit()
                conn.close()
            elif self.path == "/current":
                current_focus.clear()
                current_focus.update({
                    "app_id": data.get("app_id", ""),
                    "window_id": int(data.get("window_id", -1)),
                    "title": data.get("title", ""),
                    "start_time": float(data.get("start_time", 0.0)),
                    "updated_at": time.time(),
                })
            elif self.path == "/tasks":
                allowed_app_ids = data.get("allowed_app_ids", [])
                allowed_titles = data.get("allowed_titles", [])
                excluded = 1 if data.get("exclude", False) else 0
                done = 1 if data.get("done", False) else 0
                conn = sqlite3.connect(DB_PATH)
                cur = conn.cursor()
                cur.execute("SELECT COUNT(*) FROM focus_tasks")
                if cur.fetchone()[0] >= 1:
                    conn.close()
                    self.send_response(409)
                    self.end_headers()
                    self.wfile.write(b"only one task allowed")
                    return
                cur.execute("""
                    INSERT INTO focus_tasks
                    (category, task, start_time, end_time, allowed_app_ids, allowed_titles, excluded, done)
                    VALUES (?, ?, ?, ?, ?, ?, ?, ?)
                """, (
                    data.get("category", ""),
                    data.get("task", ""),
                    float(data.get("start_time", 0.0)),
                    float(data.get("end_time", 0.0)),
                    json.dumps(allowed_app_ids),
                    json.dumps(allowed_titles),
                    excluded,
                    done,
                ))
                conn.commit()
                conn.close()
            elif self.path == "/tasks/update":
                task_id = int(data.get("id", 0))
                if task_id <= 0:
                    self.send_response(400)
                    self.end_headers()
                    self.wfile.write(b"missing id")
                    return

                conn = sqlite3.connect(DB_PATH)
                conn.row_factory = sqlite3.Row
                cur = conn.cursor()
                cur.execute("SELECT * FROM focus_tasks WHERE id = ?", (task_id,))
                row = cur.fetchone()
                if not row:
                    conn.close()
                    self.send_response(404)
                    self.end_headers()
                    self.wfile.write(b"task not found")
                    return
                row_dict = dict(row)

                if "category" in data:
                    category = data.get("category", "")
                else:
                    category = row_dict.get("category", "")

                if "task" in data:
                    task_name = data.get("task", "")
                else:
                    task_name = row_dict.get("task", "")

                if "start_time" in data:
                    start_time = float(data.get("start_time", 0.0))
                else:
                    start_time = float(row_dict.get("start_time") or 0.0)

                if "end_time" in data:
                    end_time = float(data.get("end_time", 0.0))
                else:
                    end_time = float(row_dict.get("end_time") or 0.0)

                if "allowed_app_ids" in data:
                    allowed_app_ids = data.get("allowed_app_ids", [])
                else:
                    try:
                        allowed_app_ids = json.loads(row_dict.get("allowed_app_ids") or "[]")
                    except Exception:
                        allowed_app_ids = []

                if "allowed_titles" in data:
                    allowed_titles = data.get("allowed_titles", [])
                else:
                    try:
                        allowed_titles = json.loads(row_dict.get("allowed_titles") or "[]")
                    except Exception:
                        allowed_titles = []

                if "exclude" in data:
                    excluded = 1 if data.get("exclude", False) else 0
                else:
                    excluded = int(row_dict.get("excluded") or 0)

                if "done" in data:
                    done = 1 if data.get("done", False) else 0
                else:
                    done = int(row_dict.get("done") or 0)

                cur.execute("""
                    UPDATE focus_tasks
                    SET category = ?, task = ?, start_time = ?, end_time = ?, allowed_app_ids = ?, allowed_titles = ?, excluded = ?, done = ?
                    WHERE id = ?
                """, (
                    category,
                    task_name,
                    start_time,
                    end_time,
                    json.dumps(allowed_app_ids),
                    json.dumps(allowed_titles),
                    excluded,
                    done,
                    task_id,
                ))
                conn.commit()
                conn.close()
            else:
                self.send_response(404)
                self.end_headers()
                return
        except Exception as e:
            self.send_response(400)
            self.end_headers()
            self.wfile.write(str(e).encode())
            return

        self.send_response(200)
        self.end_headers()
        self.wfile.write(b"ok")

    def do_GET(self):
        if self.path == "/" or self.path == "/index.html":
            with open(os.path.join(BASE_DIR, "index.html"), "rb") as f:
                data = f.read()
            self.send_response(200)
            self.send_header("Content-Type", "text/html")
            self.end_headers()
            self.wfile.write(data)
            return

        if self.path == "/events":
            conn = sqlite3.connect(DB_PATH)
            conn.row_factory = sqlite3.Row
            cur = conn.cursor()
            cur.execute("""
                SELECT *
                FROM focus_events
                ORDER BY start_time
            """)
            rows = [dict(r) for r in cur.fetchall()]
            conn.close()

            body = json.dumps(rows)
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(body.encode())
            return

        if self.path == "/tasks":
            conn = sqlite3.connect(DB_PATH)
            conn.row_factory = sqlite3.Row
            cur = conn.cursor()
            cur.execute("""
                SELECT *
                FROM focus_tasks
                ORDER BY start_time
            """)
            rows = []
            for r in cur.fetchall():
                d = dict(r)
                try:
                    d["allowed_app_ids"] = json.loads(d.get("allowed_app_ids") or "[]")
                except Exception:
                    d["allowed_app_ids"] = []
                try:
                    d["allowed_titles"] = json.loads(d.get("allowed_titles") or "[]")
                except Exception:
                    d["allowed_titles"] = []
                d["excluded"] = bool(d.get("excluded", 0))
                d["done"] = bool(d.get("done", 0))
                rows.append(d)
            conn.close()

            body = json.dumps(rows)
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(body.encode())
            return

        if self.path == "/current":
            body = json.dumps(current_focus)
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(body.encode())
            return

        self.send_response(404)
        self.end_headers()

    def log_message(self, *args):
        pass  # silence

if __name__ == "__main__":
    init_db()
    print(f"Listening on http://{HOST}:{PORT}")
    HTTPServer((HOST, PORT), Handler).serve_forever()

