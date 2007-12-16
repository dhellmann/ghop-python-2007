#!/usr/bin/env python

import sys
import string
import email
import sqlite3
import time
import re
import os
import parser
import relativedelta

def tty_width():
    # Adapted from http://mail.python.org/pipermail/python-list/2000-May/033365.html
    fp = os.popen('stty -a', 'r')
    ln1 = fp.readline()
    fp.close()
    if not ln1:
       raise ValueError, 'tty size not supported for input'
    vals = {'rows':None, 'columns':None}
    for ph in string.split(ln1, ';'):
        x = string.split(ph)
        if len(x) == 2:
            vals[x[0]] = x[1]
            vals[x[1]] = x[0]
    return int(vals['columns'])

def beginning_of_day(date):
    return date + relativedelta.relativedelta(hours=0, minutes=0, seconds=0)

def end_of_day(date):
    return beginning_of_day(date) + relativedelta.relativedelta(seconds=-1, days=+1)

def pyne_to_sql(pyne_query):
    tokens = ()

    # Find all headers
    header_re = re.compile(r"(\w+):([^\"\']\w+)|(\w+):[\"\']([^\"\']+?)[\"\']")
    for m in header_re.finditer(pyne_query):
        col = m.group(1) or m.group(3)
        value = m.group(2) or m.group(4)
        tokens += ((col, value),)
    pyne_query = header_re.sub("", pyne_query)
    
    # Find any text not in a header.
    all_re = re.compile(r"[\'\"]([^\"\':]+?)[\'\"]|(?<!\w)([^\W\"\':]+)(?!\w)")
    headerless_tokens = ()
    for m in all_re.finditer(pyne_query):
        headerless_tokens += (m.group(1) or m.group(2),)

    # Build SQL Query
    query_ands = (" 1=1",)
    query_ors = ()

    # Things that match a specific header
    for (header, value) in tokens:
        header = header.capitalize()
        value = value.strip("\"\'")
        
        if header == "Date":
            try:
                base_date = parser.parse(value)
                min_date = beginning_of_day(base_date)
                max_date = end_of_day(base_date)
            except ValueError:
                m = re.match(r"(.+)\W*to\W*(.+)", value)
                if not m:
                    raise "Date range cannot be understood."
                min_date, max_date = m.group(1, 2)
                
                min_date = parser.parse(min_date)
                max_date = parser.parse(max_date)
                
                min_date = beginning_of_day(min_date)
                max_date = end_of_day(max_date)
            min_date = datetime_to_timestamp(min_date)
            max_date = datetime_to_timestamp(max_date)
            query_ands += ((" \"Date\" > %s" % min_date),
                           (" \"Date\" < %s" % max_date) )
        else:
            query_ands += (" \"%s\" LIKE \"%%%s%%\"" % (header, value),)

    # Things that match any header
    for value in headerless_tokens:
        value = value.strip("\"\'")
        for (header, _) in (stored_headers + (("Body", None),)):
            query_ors += (" \"%s\" LIKE \"%%%s%%\"" % (header, value),)

    query = "select * from emails where" + " and".join(query_ands)
    
    if len(query_ors) > 0:
        query += " and (" + " or".join(query_ors) + ")"
    
    query += ";"
    return query

def datetime_to_timestamp(dt):
    return timetuple_to_timestamp(dt.timetuple())

def timetuple_to_timestamp(tt):
    return int(time.mktime(tt))

def rfc2822_to_timestamp(rfc2822):
    fmt = "%a, %d %b %Y %H:%M:%S"
    rfc2822 = re.match(r"(.+:\d{2})", rfc2822).group(1)
    
    try:
        time_obj = time.strptime(rfc2822, fmt)  # Properly formatted dates
    except ValueError:
        try:
            time_obj = time.strptime(rfc2822, fmt[4:])  # Dates w/o day of week at start
        except ValueError:
            time_obj = time.gmtime()
    
    return int(time.mktime(time_obj))
    return timetuple_to_timestamp(time_obj)

def is_email_list(emails):
    return re.match(r".+?,", emails) and True

def format_email(email):
    m = re.match(r"\W*(.+?)\W*<(.*)>", email)
    
    if len(email) <= col_width or not m or is_email_list(email):
        return email
    
    name, email = m.group(1, 2)
    name = name.strip("\"\' ")
    
    if name:
        return name
    else:
        return email

def format_date(date):
    date = time.gmtime(date)
    if col_width <= 11:
        fmt = "%m/%d/%y"
    elif col_width < 21:
        fmt = "%b %d, %y"
    elif col_width < 27:
        fmt = "%b %d, %y %X"
    else:
        fmt = "%c"
    return time.strftime(fmt, date)

def format(value, col_index):
    col_header = stored_headers[col_index][0]
    if col_header == "From" or col_header == "To":
        value = format_email(value)
    elif col_header == "Date":
        value = format_date(value)
    
    return " " + str(value)[0:col_width - 2].ljust(col_width - 1)

def print_headers():
    col_headers = [header for (header, _) in stored_headers]
    col_headers = [col.center(col_width) for col in col_headers]
    
    print "|".join(col_headers)

def print_resultset(sql_query):  
    print_headers()
    for row in db.execute(sql_query):
        row = [format(value, i) for (i, value) in enumerate(row[0:-1])]
        print "|".join(row)

def print_extended_resultset(sql_query):
    for row in db.execute(sql_query):
        print_headers()
        body = row[6]
        row = [format(value, i) for (i, value) in enumerate(row[0:-1])]
        print "|".join(row)
        print body

def create_table_unless_exists():
    cols = ",".join(["\"%s\" %s" % (header, type) for (header, type) in stored_headers])
    cols += ",\"body\" TEXT"
    db.execute("create table if not exists emails (" + cols + ");")

def get_input_files():
    files = sys.argv[2:]
    include_stdin = False
    
    if not sys.stdin.isatty():
        include_stdin = True
    
    for file in files:
        if file == "-":
            include_stdin = True
    
    files = [open(file) for file in files if file != "-"]
    
    if include_stdin:
        files += sys.stdin,
    
    return files

def get_encoding(message):
    default_encoding = "ascii"
    
    content_type = message.get("content-type") or ""
    content_type += ";charset=%s;" % default_encoding
    
    m = re.search(r"charset=([^;]+)", content_type)
    encoding = m.group(1) or default_encoding

    if encoding == "windows-1252":
        encoding = "iso-8859-1"
    
    return encoding

def encode_failsafe(string, encoding):
    try:
        return unicode(string, encoding, "ignore")
    except LookupError:
        return string

def get_plain_payload(message):
    if message.is_multipart():
        text_plain_payload = [subemail.get_payload() for subemail in message.get_payload() if subemail.get_content_subtype() == "plain"]
        if isinstance(text_plain_payload, list):
            return "".join(text_plain_payload)
        else:
            return text_plain_payload
    else:
        return message.get_payload()

def get_attributes(message):
    attributes = ()
    encoding = get_encoding(message)

    for (header, _) in stored_headers:
        value = message.get(header)
        if header == "Date":
            value = str(rfc2822_to_timestamp(value))
        
        if value != None:
            value = encode_failsafe(value, encoding)
        
        attributes += ((header, value),)
    
    body = get_plain_payload(message)
    body = encode_failsafe(body, encoding)
    attributes += ("body", body),
    
    return attributes

def insert_message(message):
    attributes = get_attributes(message)
    
    cols = [header for (header, _) in attributes]
    values = [(value or "") for (_, value) in attributes]
    qmarks = ",".join("?" * len(cols))
    
    db.execute("insert into emails values (" + qmarks + ");", values)
    db.commit()

def is_mbox_file(f):
    old_pos = f.tell()
    is_mbox = False
    
    for line in f:
        if line.startswith("From "):
            is_mbox = True
            break
    
    f.seek(old_pos, 0)
    return is_mbox

def mbox_messages(f):
    message_text = ""
    for (i,line) in enumerate(f):
        if not line.startswith("From "):
            message_text += line
        elif i > 0:
            yield email.message_from_string(message_text)
            message_text = ""
    yield email.message_from_string(message_text)

stored_headers = (("From", "TEXT"),
                  ("To", "TEXT"),
                  ("Subject", "TEXT"),
                  ("Date", "INTEGER"),
                  ("Cc", "TEXT"),
                  ("Bcc", "TEXT"))

mode = sys.argv[1]
db = sqlite3.connect("db")

if mode != "insert":
    col_width = (tty_width() - len(stored_headers)) / len(stored_headers)

create_table_unless_exists()

if mode == "insert":
    for curr_file in get_input_files():
        if is_mbox_file(curr_file):
            for message in mbox_messages(curr_file):
                insert_message(message)
        else:
            message = email.message_from_file(curr_file)
            insert_message(message)
elif mode == "search":
    query = " ".join(sys.argv[2:])
    print_resultset(pyne_to_sql(query))
elif mode == "list":
    print_resultset("select * from emails;")
elif mode == "display":
    query = " ".join(sys.argv[2:])
    print_extended_resultset(pyne_to_sql(query))
else:
    print "Invalid mode.  Use ./pyne.py insert to add emails, ./pyne.py search to search emails, and ./pyne list to list all emails."
