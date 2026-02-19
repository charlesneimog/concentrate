import sqlite3
import os
from datetime import datetime

# SQLite path
home = os.path.expanduser("~")
db_path = os.path.join(home, ".local", "share", "concentrate", "data.sqlite")

# Connect to database
conn = sqlite3.connect(db_path)
cursor = conn.cursor()

# Query focus_log ordered by start_time
cursor.execute("""
    SELECT
        app_id,
        title,
        task_category,
        state,
        start_time,
        end_time,
        duration
    FROM focus_log
    ORDER BY start_time ASC
""")

rows = cursor.fetchall()

# Print header
print(
    "APP_ID | TITLE | CATEGORY | STATE | START_TIME | END_TIME | DURATION"
)
print("-" * 100)

for row in rows:
    app_id, title, task_category, state, start_time, end_time, duration = row

    # Convert Unix timestamps
    start_str = datetime.fromtimestamp(start_time).strftime("%d/%m/%Y %H:%M:%S")
    end_str = datetime.fromtimestamp(end_time).strftime("%d/%m/%Y %H:%M:%S")

    print(
        f"{app_id} | {title} | {task_category} | {state} | "
        f"{start_str} | {end_str} | {duration}"
    )

# Close connection
conn.close()

