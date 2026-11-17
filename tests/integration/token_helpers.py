"""Helpers shared by the API token tests (test_api_tokens*.py). Not a test module."""
import hashlib
import re
import uuid

import pytest
import requests

from conftest import USER_PASSWORD, auth, get_all

TOKEN_PATTERN = r"sst_[A-Za-z0-9_-]{43}"
FIELDS = {"id", "name", "target", "scope", "createdBy", "createdAt", "expiresAt", "lastUsedAt", "expired"}
SERVER_TARGET = {"type": "server"}


def describe(response):
    """What an assert message may say about a response: status and error text, never the body of a success
    (a create response carries the plaintext token)."""
    if response.status_code < 400:
        return f"status {response.status_code}"
    try:
        return f"status {response.status_code}: {response.json().get('error')}"
    except ValueError:
        return f"status {response.status_code}"


def sha256_hex(value):
    return hashlib.sha256(value.encode()).hexdigest()


def create_token(base_url, admin_token, **overrides):
    """POST /admin/tokens with a valid server/read_write body, overridden by keyword; a value of None drops the field."""
    body = {"name": f"app-{uuid.uuid4().hex[:12]}", "target": dict(SERVER_TARGET), "scope": "read_write"}
    body.update(overrides)
    body = {key: value for key, value in body.items() if value is not None}
    return requests.post(f"{base_url}/admin/tokens", json=body, headers=auth(admin_token), timeout=10)


def issue(base_url, admin_token, **overrides):
    """Creates a token and returns its response data (including the plaintext under "token"); asserts 201."""
    response = create_token(base_url, admin_token, **overrides)
    assert response.status_code == 201, describe(response)
    return response.json()


def list_tokens(base_url, admin_token, **params):
    return get_all(f"{base_url}/admin/tokens", auth(admin_token), **params)


def remove_all_tokens(base_url, admin_token):
    """Revokes every token (the shared test database must end up without tokens)."""
    for token in list_tokens(base_url, admin_token):
        response = requests.delete(f"{base_url}/admin/tokens/{token['id']}", headers=auth(admin_token), timeout=10)
        assert response.status_code == 204, describe(response)


def new_project(base_url, token, name=None, **extra):
    response = requests.post(f"{base_url}/projects", json={"name": name or f"tok_{uuid.uuid4().hex[:8]}", "description": "api token tests", **extra},
                             headers=auth(token), timeout=10)
    assert response.status_code == 201, describe(response)
    return response.json()


def package(name):
    return {"@type": "Package", "@id": str(uuid.uuid4()), "declaredName": name}


def commit_body(name="x"):
    return {"description": "t", "change": [{"payload": package(name)}]}


def new_commit(base_url, token, project_id, name="x"):
    response = requests.post(f"{base_url}/projects/{project_id}/commits", json=commit_body(name), headers=auth(token), timeout=10)
    assert response.status_code == 201, describe(response)
    return response.json()["@id"]


@pytest.fixture
def make_admin(base_url, admin_token):
    """Factory for a second administrator (never the main admin): returns (username, password). The users are deleted
    afterwards, because test_roles needs `admin` to be the only administrator (last administrator rules)."""
    created = []

    def _make(role=100):
        username = f"adm_{uuid.uuid4().hex[:12]}"
        response = requests.post(f"{base_url}/admin/users", json={"username": username, "password": USER_PASSWORD, "role": role},
                                 headers=auth(admin_token), timeout=10)
        assert response.status_code == 201, describe(response)
        created.append(username)
        return username, USER_PASSWORD

    yield _make
    for username in created:
        requests.delete(f"{base_url}/admin/users/{username}", headers=auth(admin_token), timeout=10)
    # the shared database must not keep an extra administrator
    users = get_all(f"{base_url}/admin/users", auth(admin_token))
    assert [u["username"] for u in users if u.get("role") == 100] == ["admin"]


def get_token_detail(base_url, admin_token, token_id):
    return requests.get(f"{base_url}/admin/tokens/{token_id}", headers=auth(admin_token), timeout=10)


def status(base_url, plaintext, path="/projects"):
    return requests.get(f"{base_url}{path}", headers=auth(plaintext), timeout=10).status_code


def server_log_text(output_path):
    """Everything a server wrote: its output file and every other file in the same directory (Drogon's own log file)."""
    texts = []
    for path in sorted(output_path.parent.rglob("*")):
        if path.is_file():
            texts.append(path.read_text(errors="replace"))
    return "\n".join(texts)
