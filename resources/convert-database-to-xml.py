import sqlite3
import os
from odf.opendocument import OpenDocumentSpreadsheet
from odf.table import Table, TableRow, TableCell
from odf.text import P

# SQLite path
home = os.path.expanduser("~")
db_path = os.path.join(home, ".local", "focusservice", "data.sqlite")

# Open SQLite database
conn = sqlite3.connect(db_path)
cursor = conn.cursor()

# Create ODS spreadsheet
doc = OpenDocumentSpreadsheet()

# Get all tables in the database
cursor.execute("SELECT name FROM sqlite_master WHERE type='table';")
tables = [row[0] for row in cursor.fetchall()]

for table_name in tables:
    # Create a new sheet for each table
    sheet = Table(name=table_name)
    doc.spreadsheet.addElement(sheet)
    
    cursor.execute(f"SELECT * FROM {table_name}")
    columns = [desc[0] for desc in cursor.description]

    # Add header row
    header_row = TableRow()
    for col_name in columns:
        cell = TableCell()
        cell.addElement(P(text=col_name))
        header_row.addElement(cell)
    sheet.addElement(header_row)

    # Add data rows
    for row in cursor.fetchall():
        table_row = TableRow()
        for value in row:
            cell = TableCell()
            cell.addElement(P(text=str(value) if value is not None else ""))
            table_row.addElement(cell)
        sheet.addElement(table_row)

# Save ODS file
output_path = os.path.join(home, "focusservice_export.ods")
doc.save(output_path)

print(f"Database exported to {output_path}")
