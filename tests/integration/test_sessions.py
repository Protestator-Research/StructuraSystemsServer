import base64
import re
import time

import pytest
import requests

from conftest import ADMIN_PASSWORD, USER_PASSWORD, auth, login, start_server

SESSION_DB = "structura_systems_sessiontest"
SESSION_PORT = 18091
NEW_PASSWORD = "BrandNewPass123!"


def status_of(base_url, token):
    return requests.get(f"{base_url}/me", headers=auth(token), timeout=10).status_code


def test_each_login_gets_its_own_token(base_url, make_user):
    name, password = make_user()
    first = login(base_url, name, password)
    second = login(base_url, name, password)
    assert first != second
    assert status_of(base_url, first) == 200
    assert status_of(base_url, second) == 200


def test_token_format(base_url, admin_token):
    assert re.fullmatch(r"[A-Za-z0-9_-]{43,}", admin_token), admin_token
    # 32 random bytes in URL safe base64 without padding.
    assert len(base64.urlsafe_b64decode(admin_token + "=")) == 32
    # The token is also sent in the Authorization header of the login response.
    response = requests.post(f"{base_url}/login", json={"username": "admin", "password": ADMIN_PASSWORD}, timeout=10)
    assert response.headers["Authorization"] == f"Bearer {response.json()['bearer']}"


def test_deleting_a_user_invalidates_all_tokens(base_url, admin_token, make_user):
    name, password = make_user()
    tokens = [login(base_url, name, password) for _ in range(2)]
    assert all(status_of(base_url, token) == 200 for token in tokens)

    response = requests.delete(f"{base_url}/admin/users/{name}", headers=auth(admin_token), timeout=10)
    assert response.status_code == 200
    assert all(status_of(base_url, token) == 401 for token in tokens)
    # The name can be reused without reviving the old tokens.
    again = requests.post(f"{base_url}/admin/users", json={"username": name, "password": USER_PASSWORD, "role": 0},
                          headers=auth(admin_token), timeout=10)
    assert again.status_code == 201
    assert all(status_of(base_url, token) == 401 for token in tokens)


def test_admin_password_reset_invalidates_all_tokens(base_url, admin_token, make_user):
    name, password = make_user()
    tokens = [login(base_url, name, password) for _ in range(2)]

    response = requests.put(f"{base_url}/admin/users/{name}", json={"password": "ResetByAdmin123!"},
                            headers=auth(admin_token), timeout=10)
    assert response.status_code == 200
    assert all(status_of(base_url, token) == 401 for token in tokens)
    # The admin's own session is unaffected.
    assert status_of(base_url, admin_token) == 200


def test_own_password_change_keeps_only_the_current_token(base_url, make_user):
    name, password = make_user()
    current = login(base_url, name, password)
    other = login(base_url, name, password)

    response = requests.put(f"{base_url}/me/password", json={"oldPassword": password, "newPassword": NEW_PASSWORD},
                            headers=auth(current), timeout=10)
    assert response.status_code == 200
    assert status_of(base_url, current) == 200
    assert status_of(base_url, other) == 401


def test_unknown_and_malformed_tokens_are_rejected(base_url):
    for token in ["", "abc", "x" * 5000, "Tw28t35IzI-Wam-a9Mcb2NPJaeeRLM-t__Q_LT_ZleI"]:
        response = requests.get(f"{base_url}/me", headers={"Authorization": f"Bearer {token}"}, timeout=10)
        assert response.status_code == 401


@pytest.fixture(scope="module")
def short_session_server():
    config = {
        "session_idle_seconds": 2,
        "session_max_seconds": 5,
        "login_max_requests_per_ip": 100000,
        "login_max_failures_per_user": 1000,
    }
    with start_server(SESSION_DB, SESSION_PORT, custom_config=config):
        yield f"http://127.0.0.1:{SESSION_PORT}"


def test_idle_timeout(short_session_server):
    base_url = short_session_server
    token = login(base_url, "admin", ADMIN_PASSWORD)
    assert status_of(base_url, token) == 200
    time.sleep(3.2)
    assert status_of(base_url, token) == 401
    # The expired token does not come back.
    assert status_of(base_url, token) == 401


def test_use_refreshes_idle_timer_until_max_lifetime(short_session_server):
    base_url = short_session_server
    started = time.monotonic()
    token = login(base_url, "admin", ADMIN_PASSWORD)

    # Used every second, so the idle timeout (2 s) never hits. It must keep working until the maximum (5 s).
    while time.monotonic() - started < 3.5:
        assert status_of(base_url, token) == 200
        time.sleep(1)

    # After the maximum lifetime the token is rejected although it was just used.
    deadline = started + 8
    while time.monotonic() < deadline:
        if status_of(base_url, token) == 401:
            assert time.monotonic() - started >= 5
            return
        time.sleep(0.5)
    pytest.fail("token was still valid after the maximum lifetime")
