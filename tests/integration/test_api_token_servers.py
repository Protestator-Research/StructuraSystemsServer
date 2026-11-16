"""API tokens on dedicated servers: expiry (test only lifetime override), the limit of tokens, lifetime configuration,
audit log and secrecy."""
import re
import time
import uuid
from types import SimpleNamespace

import pytest
import requests

from conftest import ADMIN_PASSWORD, auth, login, start_server
from token_helpers import (create_token, describe, get_token_detail, issue, list_tokens, new_project, remove_all_tokens, server_log_text,
                           status)

EXPIRY_DB = "structura_systems_apitoken_expiry"
EXPIRY_PORT = 18098
LIMIT_DB = "structura_systems_apitoken_limit"
LIMIT_PORT = 18099
LIFETIME_SECONDS = 5
LIMIT_LIFETIME_SECONDS = 8
CONFIG_DB = "structura_systems_apitoken_config"
CONFIG_PORT = 18096
CAP_DB = "structura_systems_apitoken_cap"
CAP_PORT = 18095
DAY = 86400
THROTTLE = {"login_max_requests_per_ip": 100000, "login_max_failures_per_user": 1000}


@pytest.fixture(scope="module")
def expiry_server():
    config = {**THROTTLE, "api_token_lifetime_override_seconds": LIFETIME_SECONDS}
    with start_server(EXPIRY_DB, EXPIRY_PORT, custom_config=config) as (_, output):
        base = f"http://127.0.0.1:{EXPIRY_PORT}"
        yield SimpleNamespace(base=base, admin=login(base, "admin", ADMIN_PASSWORD), output=output, plaintexts=[])


@pytest.fixture(scope="module")
def limit_server():
    config = {**THROTTLE, "api_token_max_count": 2, "api_token_lifetime_override_seconds": LIMIT_LIFETIME_SECONDS}
    with start_server(LIMIT_DB, LIMIT_PORT, custom_config=config):
        base = f"http://127.0.0.1:{LIMIT_PORT}"
        yield SimpleNamespace(base=base, admin=login(base, "admin", ADMIN_PASSWORD))


@pytest.fixture(scope="module")
def config_server():
    """Default 2 days, maximum 3 days; the override (longer than the maximum request) must not lengthen anything."""
    config = {**THROTTLE, "api_token_default_lifetime_days": 2, "api_token_max_lifetime_days": 3, "api_token_lifetime_override_seconds": 200000}
    with start_server(CONFIG_DB, CONFIG_PORT, custom_config=config):
        base = f"http://127.0.0.1:{CONFIG_PORT}"
        yield SimpleNamespace(base=base, admin=login(base, "admin", ADMIN_PASSWORD))


@pytest.fixture(scope="module")
def cap_server():
    """A configured maximum above 365 days is capped at 365 (and so is the default)."""
    config = {**THROTTLE, "api_token_default_lifetime_days": 1000, "api_token_max_lifetime_days": 1000}
    with start_server(CAP_DB, CAP_PORT, custom_config=config):
        base = f"http://127.0.0.1:{CAP_PORT}"
        yield SimpleNamespace(base=base, admin=login(base, "admin", ADMIN_PASSWORD))


def test_override_warning_is_logged(expiry_server):
    text = expiry_server.output.read_text(errors="replace")
    assert "api_token_lifetime_override_seconds is set" in text
    assert "tests only" in text
    assert "production" in text


def test_token_expires_and_its_name_becomes_free(expiry_server, mongo):
    base, admin = expiry_server.base, expiry_server.admin
    name = f"short-{uuid.uuid4().hex[:8]}"
    response = create_token(base, admin, name=name, expiresInDays=365)
    assert response.status_code == 201, describe(response)
    data = response.json()
    expiry_server.plaintexts.append(data["token"])
    # the override replaces the requested lifetime (it only shortens)
    assert data["expiresAt"] - data["createdAt"] == LIFETIME_SECONDS
    assert data["expired"] is False
    assert get_token_detail(base, admin, data["id"]).json()["expired"] is False
    assert status(base, data["token"]) == 200

    # a request for the minimum of 1 day is shortened as well
    one_day = issue(base, admin, expiresInDays=1)
    expiry_server.plaintexts.append(one_day["token"])
    assert one_day["expiresAt"] - one_day["createdAt"] == LIFETIME_SECONDS

    deadline = time.time() + 30
    while status(base, data["token"]) != 401:
        assert time.time() < deadline, "the token did not expire"
        time.sleep(0.5)
    # expired stays expired, also on routes that need more than being known
    assert status(base, data["token"], "/me") == 401

    # an expired token stays visible (flagged) until it is purged, in the database as well
    assert get_token_detail(base, admin, data["id"]).json()["expired"] is True
    assert [t["expired"] for t in list_tokens(base, admin) if t["id"] == data["id"]] == [True]
    assert mongo[EXPIRY_DB].api_tokens.find_one({"_id": data["id"]}) is not None

    # creating a token with the same name deletes the expired one (the purge itself only runs after 30 days)
    again = create_token(base, admin, name=name)
    assert again.status_code == 201, describe(again)
    expiry_server.plaintexts.append(again.json()["token"])
    assert get_token_detail(base, admin, data["id"]).status_code == 404
    assert mongo[EXPIRY_DB].api_tokens.find_one({"_id": data["id"]}) is None
    assert status(base, again.json()["token"]) == 200


def test_expired_token_can_be_revoked(expiry_server, mongo):
    base, admin = expiry_server.base, expiry_server.admin
    data = issue(base, admin)
    expiry_server.plaintexts.append(data["token"])
    deadline = time.time() + 30
    while status(base, data["token"]) != 401:
        assert time.time() < deadline, "the token did not expire"
        time.sleep(0.5)
    assert requests.delete(f"{base}/admin/tokens/{data['id']}", headers=auth(admin), timeout=10).status_code == 204
    assert get_token_detail(base, admin, data["id"]).status_code == 404
    assert mongo[EXPIRY_DB].api_tokens.find_one({"_id": data["id"]}) is None


def test_audit_log_has_ids_and_names_but_never_the_plaintext(expiry_server):
    base, admin = expiry_server.base, expiry_server.admin
    name = f"audit-{uuid.uuid4().hex[:8]}"
    data = issue(base, admin, name=name)
    plaintext = data["token"]
    expiry_server.plaintexts.append(plaintext)
    assert status(base, plaintext) == 200
    assert status(base, plaintext[:-1] + ("A" if plaintext[-1] != "A" else "B")) == 401
    assert requests.delete(f"{base}/admin/tokens/{data['id']}", headers=auth(admin), timeout=10).status_code == 204
    assert status(base, plaintext) == 401

    text = expiry_server.output.read_text(errors="replace")
    created = [line for line in text.splitlines() if "[audit] API token created" in line and data["id"] in line]
    revoked = [line for line in text.splitlines() if "[audit] API token revoked" in line and data["id"] in line]
    assert len(created) == 1 and len(revoked) == 1
    for line in created + revoked:
        assert f'name="{name}"' in line
        assert "by=admin" in line
        assert "target=server" in line

    # the other target types: target=project:<ids> and target=group:<name>, the exact line format
    project = new_project(base, admin)["@id"]
    other_project = new_project(base, admin)["@id"]
    group_name = f"audit.group{uuid.uuid4().hex[:6]}"
    project_data = issue(base, admin, target={"type": "project", "projects": [project, other_project]}, scope="read")
    group_data = issue(base, admin, target={"type": "group", "group": group_name}, scope="read_write")
    expiry_server.plaintexts += [project_data["token"], group_data["token"]]
    text = expiry_server.output.read_text(errors="replace")
    for issued, target, scope in ((project_data, f"project:{project},{other_project}", "read"), (group_data, f"group:{group_name}", "read_write")):
        pattern = (rf'\[audit\] API token created: id={issued["id"]} name="{re.escape(issued["name"])}" target={re.escape(target)} '
                   rf'scope={scope} expiresAt={issued["expiresAt"]} by=admin')
        assert len(re.findall(pattern, text)) == 1, target

    # nowhere in the output or in any other log file: not the plaintext (also not without the prefix) of any token issued here
    log = server_log_text(expiry_server.output)
    for known in expiry_server.plaintexts:
        assert known not in log
        assert known[4:] not in log


def test_limit_is_409_until_a_token_is_revoked(limit_server):
    base, admin = limit_server.base, limit_server.admin
    remove_all_tokens(base, admin)
    first = issue(base, admin)
    # below the limit a duplicate name is a 409 of its own, with its own reason
    taken = create_token(base, admin, name=first["name"])
    assert taken.status_code == 409, describe(taken)
    assert "already exists" in taken.json()["error"]
    assert "maximum" not in taken.json()["error"]

    second = issue(base, admin)
    blocked = create_token(base, admin)
    assert blocked.status_code == 409, describe(blocked)
    assert blocked.json()["error"] == "The maximum number of API tokens is reached."
    assert "already exists" not in blocked.json()["error"]

    assert requests.delete(f"{base}/admin/tokens/{first['id']}", headers=auth(admin), timeout=10).status_code == 204
    third = create_token(base, admin)
    assert third.status_code == 201, describe(third)
    assert create_token(base, admin).status_code == 409
    assert status(base, second["token"]) == 200
    assert status(base, first["token"]) == 401


def test_expired_tokens_do_not_count_against_the_limit(limit_server, mongo):
    base, admin = limit_server.base, limit_server.admin
    remove_all_tokens(base, admin)
    first, second = issue(base, admin), issue(base, admin)
    assert create_token(base, admin).status_code == 409
    deadline = time.time() + 30
    while status(base, first["token"]) != 401 or status(base, second["token"]) != 401:
        assert time.time() < deadline, "the tokens did not expire"
        time.sleep(0.5)

    # the name of an expired token is reused; the old document is gone, the other expired one is kept (no purge before 30 days)
    reused = create_token(base, admin, name=first["name"])
    assert reused.status_code == 201, describe(reused)
    assert get_token_detail(base, admin, first["id"]).status_code == 404
    assert mongo[LIMIT_DB].api_tokens.find_one({"_id": first["id"]}) is None
    assert mongo[LIMIT_DB].api_tokens.find_one({"_id": second["id"]}) is not None
    assert get_token_detail(base, admin, second["id"]).json()["expired"] is True

    # two unexpired tokens fill the limit although an expired one exists
    assert create_token(base, admin).status_code == 201
    blocked = create_token(base, admin)
    assert blocked.status_code == 409, describe(blocked)
    assert blocked.json()["error"] == "The maximum number of API tokens is reached."


def test_lifetime_configuration(config_server):
    base, admin = config_server.base, config_server.admin
    # no expiresInDays: the configured default; the override (200000 s) is longer and changes nothing
    default = issue(base, admin)
    assert default["expiresAt"] - default["createdAt"] == 2 * DAY
    # the override never lengthens a requested lifetime
    one = issue(base, admin, expiresInDays=1)
    assert one["expiresAt"] - one["createdAt"] == DAY
    maximum = issue(base, admin, expiresInDays=3)
    assert maximum["expiresAt"] - maximum["createdAt"] == 200000
    above = create_token(base, admin, expiresInDays=4)
    assert above.status_code == 400, describe(above)
    assert "expiresInDays" in above.json()["error"]


def test_configured_maximum_is_capped_at_365_days(cap_server):
    base, admin = cap_server.base, cap_server.admin
    default = issue(base, admin)
    assert default["expiresAt"] - default["createdAt"] == 365 * DAY
    maximum = issue(base, admin, expiresInDays=365)
    assert maximum["expiresAt"] - maximum["createdAt"] == 365 * DAY
    assert create_token(base, admin, expiresInDays=366).status_code == 400
