#!/usr/bin/env python3
import sqlite3
import json
import argparse
import os
import sys
import signal
from datetime import datetime

DB_PATH = os.path.expanduser("~/.local/share/opencode/opencode.db")
MAX_WIDTH = 80
TIMEOUT = 3

def timeout_handler(signum, frame):
    print("Error: database query timeout")
    sys.exit(1)

signal.signal(signal.SIGALRM, timeout_handler)

# ANSI colors
GREEN = "\033[32m"
YELLOW = "\033[38;5;228m"    # 标题最亮
WHITE = "\033[38;5;252m"     # 用户Q中等
GRAY = "\033[38;5;245m"      # Agent A稍暗
BLUE = "\033[38;5;117m"      # 时间戳浅蓝
RESET = "\033[0m"
BOLD = "\033[1m"

def truncate(text, max_len=MAX_WIDTH):
    if len(text) <= max_len:
        return text
    return text[:max_len - 3] + "..."

def truncate_lines(text, max_lines=3, max_width=MAX_WIDTH):
    lines = text.split("\n")
    result = []
    for i, line in enumerate(lines):
        if i >= max_lines:
            result.append(truncate("...", max_width))
            break
        result.append(truncate(line, max_width))
    return "\n".join(result)

def get_sessions(count):
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()
    
    # 按照最后消息时间排序，而不是创建时间
    cursor.execute("""
        SELECT s.id, s.title, s.time_created, s.time_updated,
               MAX(m.time_created) as last_msg_time
        FROM session s
        LEFT JOIN message m ON s.id = m.session_id
        GROUP BY s.id
        ORDER BY last_msg_time DESC
        LIMIT ?
    """, (count,))
    sessions = cursor.fetchall()
    conn.close()
    return sessions

def get_last_message_time(session_id):
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()
    cursor.execute("SELECT MAX(time_created) FROM message WHERE session_id = ?", (session_id,))
    result = cursor.fetchone()
    conn.close()
    return result[0] if result and result[0] else None

def get_messages(session_id, count):
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()
    
    cursor.execute("SELECT id, data FROM message WHERE session_id = ? ORDER BY time_created DESC", (session_id,))
    messages = cursor.fetchall()
    
    result = []
    for msg_id, msg_data in messages:
        data = json.loads(msg_data)
        role = data.get("role", "unknown")
        
        cursor.execute("SELECT data FROM part WHERE message_id = ? ORDER BY time_created", (msg_id,))
        parts = cursor.fetchall()
        
        text_parts = []
        for part_data, in parts:
            part = json.loads(part_data)
            part_type = part.get("type")
            if part_type in ("text", "reasoning"):
                text_parts.append(part.get("text", ""))
        
        content = "\n".join(text_parts)
        result.append({"role": role, "content": content})
        
        if len(result) >= count:
            break
    
    conn.close()
    return list(reversed(result))

def format_time(ts_ms):
    if not ts_ms:
        return "N/A"
    dt = datetime.fromtimestamp(ts_ms / 1000 + 8*3600)  # UTC+8
    return dt.strftime("%Y-%m-%d %H:%M:%S")

def print_session(index, session_id, title, time_created, messages, last_msg_time):
    # Session header
    print(f"{YELLOW}[{index}] {truncate(title, 40)}{RESET} {GREEN}{session_id}{RESET}")
    print(f"{BLUE}Last: {format_time(last_msg_time)}{RESET}")
    
    if not messages:
        print(f"{GRAY}  (no interactions){RESET}")
        return
    
    for msg in messages:
        role = msg["role"]
        content = msg["content"]
        if role == "user" and content:
            user_text = truncate_lines(content, max_lines=2)
            print(f"{WHITE}  [Q]: {user_text}{RESET}")
        elif role == "assistant" and content:
            asst_text = truncate_lines(content, max_lines=2)
            print(f"{GRAY}  [A]: {asst_text}{RESET}")

def cmd_list(count, interactions):
    signal.alarm(TIMEOUT)
    sessions = get_sessions(count)
    signal.alarm(0)
    
    total = len(sessions)
    
    # sessions[0] = newest, sessions[total-1] = oldest
    # 打印顺序：oldest at top, newest at bottom
    # 序号：newest = 0, oldest = total-1
    for i in range(total - 1, -1, -1):
        session_id, title, time_created, time_updated, last_msg_time = sessions[i]
        messages = get_messages(session_id, interactions)
        print_session(i, session_id, title, time_created, messages, last_msg_time)
        print()

def cmd_enter(index):
    signal.alarm(TIMEOUT)
    sessions = get_sessions(100)
    signal.alarm(0)
    
    total = len(sessions)
    if index < 0 or index >= total:
        print(f"Error: index {index} out of range (0-{total-1})")
        sys.exit(1)
    
    # sessions[0] = newest = display index 0
    # sessions[total-1] = oldest = display index total-1
    # So display index directly maps to sessions array index
    session_id = sessions[index][0]
    os.execvp("opencode", ["opencode", "-s", session_id])

def cmd_delete(index):
    signal.alarm(TIMEOUT)
    sessions = get_sessions(100)
    signal.alarm(0)
    
    total = len(sessions)
    if index < 0 or index >= total:
        print(f"Error: index {index} out of range (0-{total-1})")
        sys.exit(1)
    
    # Same index mapping as cmd_enter
    session = sessions[index]
    session_id = session[0]
    title = session[1]
    
    confirm = input(f"Delete session [{index}] '{truncate(title, 40)}' ({session_id})? [y/N]: ")
    if confirm.lower() != 'y':
        print("Cancelled.")
        return
    
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()
    
    # 删除 part 表中的相关数据
    cursor.execute("""
        DELETE FROM part WHERE message_id IN (
            SELECT id FROM message WHERE session_id = ?
        )
    """, (session_id,))
    
    # 删除 message 表中的相关数据
    cursor.execute("DELETE FROM message WHERE session_id = ?", (session_id,))
    
    # 删除 session 表中的数据
    cursor.execute("DELETE FROM session WHERE id = ?", (session_id,))
    
    conn.commit()
    conn.close()
    
    print(f"Deleted session [{index}] '{truncate(title, 40)}'")

def main():
    parser = argparse.ArgumentParser(description="Show recent opencode sessions")
    parser.add_argument("-c", "--count", type=int, default=10, help="Number of sessions to show (default: 10)")
    parser.add_argument("-n", "--interactions", type=int, default=5, help="Number of messages per session (default: 5)")
    parser.add_argument("-d", "--delete", type=int, metavar="INDEX", help="Delete session by index")
    parser.add_argument("action", nargs="?", help="'in' to enter a session")
    parser.add_argument("index", nargs="?", type=int, help="Session index to enter")
    args = parser.parse_args()
    
    if args.delete is not None:
        cmd_delete(args.delete)
    elif args.action == "in":
        if args.index is None:
            print("Usage: sss in <index>")
            sys.exit(1)
        cmd_enter(args.index)
    else:
        cmd_list(args.count, args.interactions)

if __name__ == "__main__":
    main()
