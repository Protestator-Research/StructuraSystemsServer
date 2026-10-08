import json
import os
import subprocess
import tempfile
import time
import uuid
from contextlib import contextmanager
from pathlib import Path
from urllib.parse import quote

import pytest
import requests
from pymongo import MongoClient

REPO_ROOT = Path(__file__).resolve().parents[2]
# STRUCTURA_SERVER_BINARY allows running the tests against another build, e.g. an ASan build.
SERVER_BINARY = Path(os.environ.get(
    "STRUCTURA_SERVER_BINARY",
    str(REPO_ROOT / "build" / "Debug" / "debug" / "StructuraSystemsServer"),
))


def find_conan_run_env():
    """STRUCTURA_CONAN_RUN_ENV wins; otherwise conanrun.sh next to the binary's build folder, then the Debug build."""
    override = os.environ.get("STRUCTURA_CONAN_RUN_ENV")
    if override:
        return Path(override)
    for candidate in (
        SERVER_BINARY.parent.parent / "generators" / "conanrun.sh",
        REPO_ROOT / "build" / "Debug" / "generators" / "conanrun.sh",
    ):
        if candidate.exists():
            return candidate
    return None


CONAN_RUN_ENV = find_conan_run_env()
TEST_DB = "structura_systems_test"
TEST_PORT = 18088
ADMIN_PASSWORD = "TestAdmin123!"
USER_PASSWORD = "Password1234!"
# Credentials of the Mongo instance used by the tests, also handed to the server under test.
MONGO_USER = os.environ.get("TEST_MONGO_USER", "root")
MONGO_PASSWORD = os.environ.get("TEST_MONGO_PASSWORD", "root-password")
MONGO_URI = os.environ.get(
    "TEST_MONGO_URI",
    f"mongodb://{quote(MONGO_USER, safe='')}:{quote(MONGO_PASSWORD, safe='')}@localhost:27017/?authSource=admin",
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


def wait_for_ready(proc, base_url, dump_output, timeout=60):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if proc.poll() is not None:
            dump_output()
            pytest.fail(f"server exited early with code {proc.returncode}")
        try:
            requests.post(f"{base_url}/login", json={}, timeout=2)
            return
        except requests.exceptions.RequestException:
            time.sleep(0.5)
    dump_output()
    pytest.fail(f"server did not become ready within {timeout} s")


def server_command():
    # The Conan run environment provides the shared library search path.
    if CONAN_RUN_ENV is not None:
        return ["bash", "-c", 'source "$1" && exec "$2"', "bash",
                str(CONAN_RUN_ENV), str(SERVER_BINARY)]
    return [str(SERVER_BINARY)]


@contextmanager
def start_server(db_name, port, extra_env=None, drop_db=True, expect_ready=True, custom_config=None):
    """Start a server on the given database and yield (process, output_path).

    With expect_ready=False the caller deals with start-up failures itself.
    custom_config entries are added to (and override) the custom_config section of the config file.
    """
    assert db_name != "structura_systems"
    assert db_name.startswith("structura_systems_")
    if drop_db:
        client = MongoClient(MONGO_URI, serverSelectionTimeoutMS=5000)
        client.drop_database(db_name)
        client.close()

    tmp = Path(tempfile.mkdtemp(prefix="structura-test-"))
    config = {
        "listeners": [{"address": "127.0.0.1", "port": port, "https": False}],
        "custom_config": {
            "mongo_address": "localhost:27017",
            "mongo_username": MONGO_USER,
            "mongo_password": MONGO_PASSWORD,
            "mongo_database": db_name,
            "allowed_origins": [],
        },
        "log": {
            "log_path": str(tmp),
            "logfile_base_name": "",
            "log_size_limit": 100000000,
            "log_level": "INFO",
        },
    }
    config["custom_config"].update(custom_config or {})
    config_path = tmp / "config.json"
    config_path.write_text(json.dumps(config))
    output_path = tmp / "server.out"

    env = dict(os.environ)
    env["STRUCTURA_CONFIG"] = str(config_path)
    env["STRUCTURA_ADMIN_PASSWORD"] = ADMIN_PASSWORD
    for name in ("MONGO_ADDRESS", "MONGO_USERNAME", "MONGO_PASSWORD", "MONGO_DATABASE", "MONGO_TLS"):
        env.pop(name, None)
    env.update(extra_env or {})
    # A value of None removes the variable from the environment.
    env = {key: value for key, value in env.items() if value is not None}

    def dump_output():
        print("---- server output ----")
        print(output_path.read_text(errors="replace"))

    with open(output_path, "wb") as out:
        proc = subprocess.Popen(
            server_command(),
            cwd=str(REPO_ROOT),
            env=env,
            stdout=out,
            stderr=subprocess.STDOUT,
        )
        try:
            if expect_ready:
                wait_for_ready(proc, f"http://127.0.0.1:{port}", dump_output)
            yield proc, output_path
        except BaseException:
            dump_output()
            raise
        finally:
            if proc.poll() is None:
                proc.terminate()
            try:
                proc.wait(timeout=15)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()


@pytest.fixture(scope="session")
def server(base_url):
    assert TEST_DB != "structura_systems"
    # Many tests log in from 127.0.0.1, so the throttle limits are far above anything the suite reaches.
    limits = {"login_max_requests_per_ip": 100000, "login_max_failures_per_user": 1000}
    with start_server(TEST_DB, TEST_PORT, custom_config=limits) as (proc, _):
        yield proc


@pytest.fixture(scope="session")
def mongo():
    client = MongoClient(MONGO_URI, serverSelectionTimeoutMS=5000)
    yield client
    client.close()


@pytest.fixture(scope="session")
def admin_token(server, base_url):
    return login(base_url, "admin", ADMIN_PASSWORD)


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


@pytest.fixture
def make_login(base_url, make_user):
    """Creates a user and returns (username, token)."""
    def _make(role=0, group=""):
        username, password = make_user(role=role, group=group)
        return username, login(base_url, username, password)

    return _make
