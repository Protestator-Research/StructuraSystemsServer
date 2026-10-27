import statistics
import time
import uuid

import pytest
import requests

from conftest import ADMIN_PASSWORD, USER_PASSWORD, auth, login, start_server

THROTTLE_DB = "structura_systems_throttletest"
USER_PORT = 18092
IP_PORT = 18093
PROXY_PORT = 18094
FAILURE_WINDOW = 4

# High enough that only the limit under test can trigger.
HIGH = 100000


def try_login(base_url, username, password, headers=None):
    return requests.post(f"{base_url}/login", json={"username": username, "password": password},
                         headers=headers, timeout=30)


def assert_throttled(response, max_retry):
    assert response.status_code == 429
    assert response.json() == {"error": "too_many_requests"}
    retry_after = int(response.headers["Retry-After"])
    assert 1 <= retry_after <= max_retry


@pytest.fixture(scope="module")
def user_server():
    """Failure limit per user: 5 per FAILURE_WINDOW seconds. The IP limit is out of the way."""
    config = {
        "trusted_proxies": [],
        "login_max_requests_per_ip": HIGH,
        "login_max_failures_per_user": 5,
        "login_failure_window_seconds": FAILURE_WINDOW,
    }
    with start_server(THROTTLE_DB, USER_PORT, custom_config=config):
        yield f"http://127.0.0.1:{USER_PORT}"


@pytest.fixture
def new_user(user_server):
    admin = login(user_server, "admin", ADMIN_PASSWORD)

    def _make():
        name = f"user_{uuid.uuid4().hex[:12]}"
        response = requests.post(f"{user_server}/admin/users",
                                 json={"username": name, "password": USER_PASSWORD, "role": 0},
                                 headers=auth(admin), timeout=10)
        assert response.status_code == 201
        return name

    return _make


def test_sixth_wrong_password_is_throttled(user_server, new_user):
    name = new_user()
    for _ in range(5):
        assert try_login(user_server, name, "WrongPassword123!").status_code == 401
    assert_throttled(try_login(user_server, name, "WrongPassword123!"), FAILURE_WINDOW)
    # The correct password is refused while the limit is active.
    assert_throttled(try_login(user_server, name, USER_PASSWORD), FAILURE_WINDOW)
    # Other users are not affected.
    other = new_user()
    assert try_login(user_server, other, USER_PASSWORD).status_code == 200


def test_unknown_usernames_are_throttled_like_known_ones(user_server):
    name = f"nobody_{uuid.uuid4().hex[:8]}"
    for _ in range(5):
        assert try_login(user_server, name, "WrongPassword123!").status_code == 401
    assert_throttled(try_login(user_server, name, "WrongPassword123!"), FAILURE_WINDOW)


def test_successful_login_does_not_reset_failures(user_server, new_user):
    name = new_user()
    for _ in range(3):
        assert try_login(user_server, name, "WrongPassword123!").status_code == 401
    assert try_login(user_server, name, USER_PASSWORD).status_code == 200
    for _ in range(2):
        assert try_login(user_server, name, "WrongPassword123!").status_code == 401
    assert_throttled(try_login(user_server, name, USER_PASSWORD), FAILURE_WINDOW)


def test_limit_expires_with_the_window(user_server, new_user):
    name = new_user()
    for _ in range(5):
        assert try_login(user_server, name, "WrongPassword123!").status_code == 401
    refused = try_login(user_server, name, USER_PASSWORD)
    assert_throttled(refused, FAILURE_WINDOW)
    time.sleep(int(refused.headers["Retry-After"]) + 1)
    assert try_login(user_server, name, USER_PASSWORD).status_code == 200


def test_unknown_user_takes_as_long_as_wrong_password(user_server, new_user):
    known = [new_user() for _ in range(5)]
    # Warm up the connection and the server.
    try_login(user_server, "warmup", "WrongPassword123!")

    known_times, unknown_times = [], []
    for name in known:
        start = time.perf_counter()
        assert try_login(user_server, name, "WrongPassword123!").status_code == 401
        known_times.append(time.perf_counter() - start)

        start = time.perf_counter()
        assert try_login(user_server, f"nobody_{uuid.uuid4().hex[:8]}", "WrongPassword123!").status_code == 401
        unknown_times.append(time.perf_counter() - start)

    ratio = statistics.median(unknown_times) / statistics.median(known_times)
    assert 0.5 <= ratio <= 2, f"known {known_times}, unknown {unknown_times}"
    # Both paths run the password hash, so neither can be near-instant.
    assert statistics.median(unknown_times) > 0.005


def test_spoofed_forwarded_for_does_not_bypass_ip_limit():
    """trusted_proxies is empty: X-Forwarded-For must be ignored."""
    config = {
        "trusted_proxies": [],
        "login_max_requests_per_ip": 20,
        "login_ip_window_seconds": 60,
        "login_max_failures_per_user": HIGH,
    }
    with start_server(THROTTLE_DB, IP_PORT, custom_config=config):
        base_url = f"http://127.0.0.1:{IP_PORT}"
        codes = []
        for i in range(30):
            response = try_login(base_url, f"nobody_{i}", "WrongPassword123!", headers={"X-Forwarded-For": f"10.1.{i}.1"})
            codes.append(response.status_code)
            if response.status_code == 429:
                assert_throttled(response, 60)
        # The start-up probe already used one request, so the 20th request or earlier is refused.
        first_refused = codes.index(429)
        assert 15 <= first_refused <= 20, codes
        assert set(codes[:first_refused]) == {401}
        assert set(codes[first_refused:]) == {429}, codes


def test_trusted_proxy_counts_forwarded_addresses_separately():
    config = {
        "trusted_proxies": ["127.0.0.1"],
        "login_max_requests_per_ip": 5,
        "login_ip_window_seconds": 60,
        "login_max_failures_per_user": HIGH,
    }
    with start_server(THROTTLE_DB, PROXY_PORT, custom_config=config):
        base_url = f"http://127.0.0.1:{PROXY_PORT}"
        # Many different clients behind the proxy are not throttled.
        for i in range(15):
            response = try_login(base_url, f"nobody_{i}", "WrongPassword123!", headers={"X-Forwarded-For": f"10.2.{i}.1"})
            assert response.status_code == 401

        # The same client is throttled after 5 requests, using only the first entry of the header.
        codes = [
            try_login(base_url, f"nobody_{i}", "WrongPassword123!",
                      headers={"X-Forwarded-For": f"10.3.0.1 , 192.0.2.{i}"}).status_code
            for i in range(8)
        ]
        assert codes[:5] == [401] * 5, codes
        assert codes[5:] == [429] * 3, codes

        # Another client is still fine.
        assert try_login(base_url, "nobody", "WrongPassword123!", headers={"X-Forwarded-For": "10.3.0.2"}).status_code == 401
