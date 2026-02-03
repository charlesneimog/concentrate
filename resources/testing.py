import sqlite3
import os
from datetime import datetime, timedelta

# ⚠️ CHANGE THIS TO EXACTLY MATCH YOUR C++ PATH
db_path = os.path.expanduser("~/.local/Concentrate/data.sqlite")

print("Using database:", db_path)

conn = sqlite3.connect(db_path)
cursor = conn.cursor()

# Ensure table exists (safe even if already created)
cursor.execute("""
CREATE TABLE IF NOT EXISTS focus_log (
    app_id TEXT,
    title TEXT,
    task_category TEXT DEFAULT '',
    state INTEGER,
    start_time REAL NOT NULL,
    end_time REAL NOT NULL,
    duration REAL NOT NULL
)
""")

# Verify table
cursor.execute("""
SELECT name FROM sqlite_master
WHERE type='table' AND name='focus_log'
""")

if cursor.fetchone() is None:
    raise RuntimeError("focus_log table still missing — wrong database file")

# ─────────────────────────────
# Insert test data

from datetime import datetime, timedelta

now = datetime.now()

def midnight(dt):
    return dt.replace(hour=0, minute=0, second=0, microsecond=0)

# ─────────────────────────────────────
# Cross-midnight test

# Yesterday 23:30 → 23:59
d1_start = midnight(now) - timedelta(minutes=30)
d1_end   = midnight(now) - timedelta(minutes=1)

# Today 00:00 → 00:30
d2_start = midnight(now)
d2_end   = d2_start + timedelta(minutes=30)

events = [
    ("test.app", "Before Midnight", "testing", 1, d1_start, d1_end),
    ("test.app", "After Midnight",  "testing", 1, d2_start, d2_end),
]

for app_id, title, category, state, start, end in events:
    cursor.execute(
        """
        INSERT INTO focus_log
        (app_id, title, task_category, state, start_time, end_time, duration)
        VALUES (?, ?, ?, ?, ?, ?, ?)
        """,
        (
            app_id,
            title,
            category,
            state,
            start.timestamp(),
            end.timestamp(),
            (end - start).total_seconds(),
        ),
    )

conn.commit()

print("Inserted cross-midnight test sessions:")
print(f"- {d1_start} → {d1_end}")
print(f"- {d2_start} → {d2_end}")
