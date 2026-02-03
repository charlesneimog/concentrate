import sqlite3
import os
from datetime import datetime, timedelta

# ─────────────────────────────────────
# DATABASE PATH — MUST MATCH C++ EXACTLY
db_path = os.path.expanduser("~/.local/Concentrate/data.sqlite")

print("Using database:", db_path)

if not os.path.exists(db_path):
    raise FileNotFoundError("Database file does not exist. Aborting.")

# ─────────────────────────────────────
# Connect
conn = sqlite3.connect(db_path)
cursor = conn.cursor()

# Safety check: table must exist
cursor.execute("""
SELECT name FROM sqlite_master
WHERE type='table' AND name='focus_log'
""")
if cursor.fetchone() is None:
    conn.close()
    raise RuntimeError("focus_log table not found. Aborting.")

# ─────────────────────────────────────
# Time helpers (LOCAL TIME)

now = datetime.now()

def local_midnight(dt: datetime) -> datetime:
    return dt.replace(hour=0, minute=0, second=0, microsecond=0)

today_midnight = local_midnight(now)
today_midnight_ts = today_midnight.timestamp()

print("Local midnight:", today_midnight)

# ─────────────────────────────────────
# DELETE today's entries (after 00:00 local)

cursor.execute(
    """
    DELETE FROM focus_log
    WHERE start_time >= ?
    """,
    (today_midnight_ts,),
)

deleted = cursor.rowcount
print(f"Deleted {deleted} focus_log entries from today")

# ─────────────────────────────────────
# INSERT new event: 10:30 → 11:53

start_time = today_midnight + timedelta(hours=10, minutes=30)
end_time   = today_midnight + timedelta(hours=11, minutes=53)

start_ts = start_time.timestamp()
end_ts = end_time.timestamp()
duration = end_ts - start_ts

cursor.execute(
    """
    INSERT INTO focus_log
    (app_id, title, task_category, state, start_time, end_time, duration)
    VALUES (?, ?, ?, ?, ?, ?, ?)
    """,
    (
        "app.zen_browser.zen",
        "Meet - KnobDetox2026",
        "Meet",
        1,
        start_ts,
        end_ts,
        duration,
    ),
)

conn.commit()
conn.close()

print("Inserted event:")
print(f"- {start_time} → {end_time} ({duration / 60:.1f} min)")
