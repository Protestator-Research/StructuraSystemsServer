import json
import os
import subprocess
import tempfile
import time
import uuid
from pathlib import Path

import pytest
import requests
from pymongo import MongoClient

REPO_ROOT = Path(__file__).resolve().parents[2]
SERVER_BINARY = REPO_ROOT / "build" / "Debug" / "debug" / "StructuraSystemsServer"
CONAN_RUN_ENV = REPO_ROOT / "build" / "Debug" / "generators" / "conanrun.sh"
TEST_DB = "structura_systems_test"
TEST_PORT = 18088
ADMIN_PASSWORD = "TestAdmin123!"
LEGACY_ADMIN_PASSWORD = "admin"
USER_PASSWORD = "Password1234!"
MONGO_URI = os.environ.get(
    "TEST_MONGO_URI",
    "mongodb://root:root-password@localhost:27017/?authSource=admin",
)


def login(base_url, username, password):
    response = requests.post(
        f"{base_url}/login",
        json={"username": username, "password": password},
        timeout=10,
    )
    response.raise_for_status()
    return response.json()["bearer"]


def auth(token):
    return {"Authorization": f"Bearer {token}"}


@pytest.fixture(scope="session")
def base_url():
    return f"http://127.0.0.1:{TEST_PORT}"


@pytest.fixture(scope="session")
def server(base_url):
    assert TEST_DB != "structura_systems"
    client = MongoClient(MONGO_URI, serverSelectionTimeoutMS=5000)
    client.drop_database(TEST_DB)
    client.close()

    tmp = Path(tempfile.mkdtemp(prefix="structura-test-"))
    config = {
        "listeners": [{"address": "127.0.0.1", "port": TEST_PORT, "https": False}],
        "custom_config": {
            "mongo_address": "localhost:27017",
            "mongo_username": "root",
            "mongo_password": "root-password",
            "mongo_database": TEST_DB,
            "allowed_origins": [],
        },
        "log": {
            "log_path": str(tmp),
            "logfile_base_name": "",
            "log_size_limit": 100000000,
            "log_level": "INFO",
        },
    }
    config_path = tmp / "config.json"
    config_path.write_text(json.dumps(config))
    output_path = tmp / "server.out"

    env = dict(os.environ)
    env["STRUCTURA_CONFIG"] = str(config_path)
    env["STRUCTURA_ADMIN_PASSWORD"] = ADMIN_PASSWORD
    for name in ("MONGO_ADDRESS", "MONGO_USERNAME", "MONGO_PASSWORD", "MONGO_DATABASE"):
        env.pop(name, None)

    with open(output_path, "wb") as out:
        # The Conan run environment provides the shared library search path.
        if CONAN_RUN_ENV.exists():
            command = ["bash", "-c", 'source "$1" && exec "$2"', "bash",
                       str(CONAN_RUN_ENV), str(SERVER_BINARY)]
        else:
            command = [str(SERVER_BINARY)]
        proc = subprocess.Popen(
            command,
            cwd=str(REPO_ROOT),
            env=env,
            stdout=out,
            stderr=subprocess.STDOUT,
        )

        def dump_output():
            print("---- server output ----")
            print(output_path.read_text(errors="replace"))

        try:
            deadline = time.time() + 60
            ready = False
            while time.time() < deadline:
                if proc.poll() is not None:
                    dump_output()
                    pytest.fail(f"server exited early with code {proc.returncode}")
                try:
                    requests.post(f"{base_url}/login", json={}, timeout=2)
                    ready = True
                    break
                except requests.exceptions.RequestException:
                    time.sleep(0.5)
            if not ready:
                dump_output()
                pytest.fail("server did not become ready within 60 s")
            yield proc
        except BaseException:
            dump_output()
            raise
        finally:
            proc.terminate()
            try:
                proc.wait(timeout=15)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()


@pytest.fixture(scope="session")
def admin_token(server, base_url):
    try:
        return login(base_url, "admin", ADMIN_PASSWORD)
    except requests.exceptions.HTTPError:
        return login(base_url, "admin", LEGACY_ADMIN_PASSWORD)


@pytest.fixture
def make_user(base_url, admin_token):
    def _make(role=0, group=""):
        username = f"user_{uuid.uuid4().hex[:12]}"
        response = requests.post(
            f"{base_url}/admin/users",
            json={
                "username": username,
                "password": USER_PASSWORD,
                "role": role,
                "group": group,
            },
            headers=auth(admin_token),
            timeout=10,
        )
        assert response.status_code == 201, response.text
        return username, USER_PASSWORD

    return _make
