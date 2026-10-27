import re

import requests
from pymongo import MongoClient

from conftest import MONGO_URI, auth, start_server

BOOT_DB = "structura_systems_admintest"
BOOT_PORT = 18090
assert BOOT_DB != "structura_systems"


def test_generated_initial_admin_password():
    client = MongoClient(MONGO_URI, serverSelectionTimeoutMS=5000)
    try:
        with start_server(BOOT_DB, BOOT_PORT, extra_env={"STRUCTURA_ADMIN_PASSWORD": None}) as (proc, output):
            log = output.read_text(errors="replace")
            match = re.search(r"Password: (\S+)", log)
            assert match, log
            password = match.group(1)
            assert len(password) == 24
            assert password.isalnum()
            assert log.count(password) == 1

            base = f"http://127.0.0.1:{BOOT_PORT}"
            legacy = requests.post(f"{base}/login", json={"username": "admin", "password": "admin"}, timeout=10)
            assert legacy.status_code == 401
            login = requests.post(f"{base}/login", json={"username": "admin", "password": password}, timeout=10)
            assert login.status_code == 200
            token = login.json()["bearer"]

            me = requests.get(f"{base}/me", headers=auth(token), timeout=10)
            assert me.status_code == 200
            assert me.json()["mustChangePassword"] is True
            blocked = requests.get(f"{base}/projects", headers=auth(token), timeout=10)
            assert blocked.status_code == 403
            assert blocked.json() == {"error": "password_change_required"}

            assert client[BOOT_DB].users.find_one({"_id": "admin"})["mustChangePassword"] is True
    finally:
        client.drop_database(BOOT_DB)
        client.close()
