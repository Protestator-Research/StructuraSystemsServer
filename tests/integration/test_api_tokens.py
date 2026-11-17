"""API tokens, administration: issuing, validation, storage, who may manage tokens, tampering, restart, pagination.
(Access rules of the tokens: test_api_token_access.py; dedicated servers: test_api_token_servers.py.)"""
import re
import uuid

import pytest
import requests
from bson.int64 import Int64

from conftest import ADMIN_PASSWORD, LIBRARY_DESCRIPTION, TEST_DB, USER_PASSWORD, auth, get_all, login, start_server
from token_helpers import (make_admin, FIELDS, TOKEN_PATTERN, create_token, describe, get_token_detail, issue, list_tokens, new_project,
                           remove_all_tokens, sha256_hex, status)

RESTART_DB = "structura_systems_apitokens"
RESTART_PORT = 18097
BROKEN_DB = "structura_systems_apitoken_broken"
BROKEN_PORT = 18094
DAY = 86400


@pytest.fixture(scope="module", autouse=True)
def remove_tokens_afterwards(base_url, admin_token):
    """The shared test database must end up without tokens (test_migration expects an empty api_tokens collection)."""
    yield
    remove_all_tokens(base_url, admin_token)


@pytest.fixture
def server_token(base_url, admin_token):
    """A read_write server token: (id, plaintext)."""
    data = issue(base_url, admin_token)
    return data["id"], data["token"]


# --- issuing, listing, detail, revoke ---------------------------------------------------------------------------------


def test_create_list_and_detail(base_url, admin_token):
    response = create_token(base_url, admin_token, name="ci importer.1", expiresInDays=30)
    assert response.status_code == 201, describe(response)
    data = response.json()
    assert FIELDS | {"token"} == set(data)
    assert re.fullmatch(TOKEN_PATTERN, data["token"])
    assert data["name"] == "ci importer.1"
    assert data["target"] == {"type": "server"}
    assert data["scope"] == "read_write"
    assert data["createdBy"] == "admin"
    assert data["lastUsedAt"] == 0
    assert data["expired"] is False
    assert data["expiresAt"] - data["createdAt"] == 30 * DAY
    plaintext = data["token"]

    detail = get_token_detail(base_url, admin_token, data["id"])
    assert detail.status_code == 200
    expected = {key: value for key, value in data.items() if key != "token"}
    assert detail.json() == expected

    listing = requests.get(f"{base_url}/admin/tokens", headers=auth(admin_token), timeout=10)
    assert listing.status_code == 200
    assert expected in get_all(f"{base_url}/admin/tokens", auth(admin_token))
    for text in (detail.text, listing.text):
        assert plaintext not in text
        assert "tokenHash" not in text and "token\"" not in text
        assert not re.search(r"[0-9a-f]{64}", text)


def test_project_and_group_target_responses(base_url, admin_token):
    project_id = new_project(base_url, admin_token, "token-target")["@id"]
    data = issue(base_url, admin_token, target={"type": "project", "projects": [project_id.upper()]})
    assert data["target"] == {"type": "project", "projects": [project_id.lower()]}

    data = issue(base_url, admin_token, target={"type": "group", "group": "team.a"}, scope="read")
    assert data["target"] == {"type": "group", "group": "team.a"}
    assert data["scope"] == "read"


def test_plaintext_is_returned_once_and_not_cacheable(base_url, admin_token):
    response = create_token(base_url, admin_token)
    assert response.status_code == 201
    assert response.headers["Cache-Control"] == "no-store"
    data = response.json()
    plaintext = data["token"]
    for path in ("", f"/{data['id']}"):
        read = requests.get(f"{base_url}/admin/tokens{path}", headers=auth(admin_token), timeout=10)
        assert read.status_code == 200
        assert plaintext not in read.text
    # the listing is read completely
    assert all("token" not in item for item in list_tokens(base_url, admin_token))


def test_token_authenticates_model_api(base_url, server_token):
    _, plaintext = server_token
    assert status(base_url, plaintext, "/projects") == 200
    assert status(base_url, plaintext, "/version") == 200


def test_token_cannot_use_session_routes(base_url, server_token):
    _, plaintext = server_token
    for path in ("/admin/tokens", "/me"):
        response = requests.get(f"{base_url}{path}", headers=auth(plaintext), timeout=10)
        assert response.status_code == 403, path
        assert response.json() == {"error": "session_required"}


def test_revoke(base_url, admin_token, server_token, mongo):
    token_id, plaintext = server_token
    assert status(base_url, plaintext) == 200
    assert mongo[TEST_DB].api_tokens.find_one({"_id": token_id}) is not None
    response = requests.delete(f"{base_url}/admin/tokens/{token_id}", headers=auth(admin_token), timeout=10)
    assert response.status_code == 204
    assert response.content == b""
    assert mongo[TEST_DB].api_tokens.find_one({"_id": token_id}) is None
    assert status(base_url, plaintext) == 401
    assert get_token_detail(base_url, admin_token, token_id).status_code == 404
    assert requests.delete(f"{base_url}/admin/tokens/{token_id}", headers=auth(admin_token), timeout=10).status_code == 404
    assert token_id not in [token["id"] for token in list_tokens(base_url, admin_token)]


def test_upper_case_id_finds_the_token(base_url, admin_token):
    data = issue(base_url, admin_token)
    token_id = data["id"]
    assert token_id == token_id.lower()
    upper = get_token_detail(base_url, admin_token, token_id.upper())
    assert upper.status_code == 200
    assert upper.json()["id"] == token_id
    revoked = requests.delete(f"{base_url}/admin/tokens/{token_id.upper()}", headers=auth(admin_token), timeout=10)
    assert revoked.status_code == 204
    assert status(base_url, data["token"]) == 401
    assert get_token_detail(base_url, admin_token, token_id).status_code == 404


def test_unknown_and_malformed_ids_give_the_same_404(base_url, admin_token):
    bodies = set()
    for token_id in (str(uuid.uuid4()), "not-a-uuid", "1", "0" * 32):
        for method in (requests.get, requests.delete):
            response = method(f"{base_url}/admin/tokens/{token_id}", headers=auth(admin_token), timeout=10)
            assert response.status_code == 404, (method.__name__, token_id)
            bodies.add(response.text)
    assert len(bodies) == 1


def test_unauthenticated_is_401(base_url):
    assert requests.get(f"{base_url}/admin/tokens", timeout=10).status_code == 401
    assert requests.post(f"{base_url}/admin/tokens", json={}, timeout=10).status_code == 401


def test_duplicate_name_is_409(base_url, admin_token):
    name = f"dup-{uuid.uuid4().hex[:12]}"
    assert create_token(base_url, admin_token, name=name).status_code == 201
    response = create_token(base_url, admin_token, name=name, scope="read")
    assert response.status_code == 409
    assert response.json()["error"]


# --- validation -------------------------------------------------------------------------------------------------------


def test_invalid_requests(base_url, admin_token):
    project = new_project(base_url, admin_token, "token-valid")["@id"]
    server = {"type": "server"}
    cases = {
        "missing scope": dict(scope=None),
        "invalid scope": dict(scope="write"),
        "missing target": dict(target=None),
        "target not an object": dict(target="server"),
        "unknown target type": dict(target={"type": "everything"}),
        "project without projects": dict(target={"type": "project"}),
        "project with empty projects": dict(target={"type": "project", "projects": []}),
        "project with group": dict(target={"type": "project", "projects": [project], "group": "g"}),
        "project with invalid id": dict(target={"type": "project", "projects": ["nope"]}),
        "project duplicate": dict(target={"type": "project", "projects": [project, project.upper()]}),
        "unknown project": dict(target={"type": "project", "projects": [str(uuid.uuid4())]}),
        "group without group": dict(target={"type": "group"}),
        "group with invalid name": dict(target={"type": "group", "group": "a b"}),
        "group with projects": dict(target={"type": "group", "group": "g", "projects": [project]}),
        "server with group": dict(target={"type": "server", "group": "g"}),
        "missing name": dict(name=None),
        "invalid name": dict(name="bad/name"),
        "expiresInDays 0": dict(expiresInDays=0),
        "expiresInDays 366": dict(expiresInDays=366),
    }
    for label, overrides in cases.items():
        overrides.setdefault("target", server)
        response = create_token(base_url, admin_token, **overrides)
        assert response.status_code == 400, (label, describe(response))
        assert response.json()["error"], label
    for raw in ("[]", "not json", "42"):
        response = requests.post(f"{base_url}/admin/tokens", data=raw, headers={**auth(admin_token), "Content-Type": "application/json"}, timeout=10)
        assert response.status_code == 400, raw


def test_library_project_is_rejected(base_url, admin_token):
    projects = get_all(f"{base_url}/projects", auth(admin_token))
    libraries = [p for p in projects if p.get("description") == LIBRARY_DESCRIPTION]
    assert libraries, "the server fixture seeds a library project"
    response = create_token(base_url, admin_token, target={"type": "project", "projects": [libraries[0]["@id"]]})
    assert response.status_code == 400
    assert "Library" in response.json()["error"]


def test_default_and_boundary_lifetimes(base_url, admin_token):
    default = issue(base_url, admin_token)
    assert default["expiresAt"] - default["createdAt"] == 90 * DAY
    for days in (1, 365):
        data = issue(base_url, admin_token, expiresInDays=days)
        assert data["expiresAt"] - data["createdAt"] == days * DAY


@pytest.mark.parametrize("days", [-1, 2 ** 63, 2 ** 64, 1.5, "30", True, None, [30]], ids=repr)
def test_invalid_lifetimes(base_url, admin_token, days):
    # None is an explicit JSON null (the helper would drop the key), so the body is sent by hand
    body = {"name": f"life-{uuid.uuid4().hex[:8]}", "target": {"type": "server"}, "scope": "read", "expiresInDays": days}
    response = requests.post(f"{base_url}/admin/tokens", json=body, headers=auth(admin_token), timeout=10)
    assert response.status_code == 400, describe(response)
    assert "expiresInDays" in response.json()["error"]


@pytest.mark.parametrize("name", ["", "x" * 65, "line\nbreak", "tab\tname", "äpp", "a/b", "name;"], ids=repr)
def test_invalid_names(base_url, admin_token, name):
    response = create_token(base_url, admin_token, name=name)
    assert response.status_code == 400, describe(response)
    assert "name" in response.json()["error"]


def test_valid_name_boundaries(base_url, admin_token):
    assert issue(base_url, admin_token, name="x" * 64)["name"] == "x" * 64
    assert issue(base_url, admin_token, name="A b.c_d-9")["name"] == "A b.c_d-9"


def test_invalid_targets(base_url, admin_token):
    too_many = [str(uuid.uuid4()) for _ in range(51)]
    response = create_token(base_url, admin_token, target={"type": "project", "projects": too_many})
    assert response.status_code == 400
    assert "1 to 50" in response.json()["error"]
    # 50 is within the limit, so only the unknown projects are the reason
    response = create_token(base_url, admin_token, target={"type": "project", "projects": too_many[:50]})
    assert response.status_code == 400
    assert "Unknown project" in response.json()["error"]

    project = new_project(base_url, admin_token)["@id"]
    for entry in (5, None, True, {"id": project}, [project]):
        response = create_token(base_url, admin_token, target={"type": "project", "projects": [project, entry]})
        assert response.status_code == 400, repr(entry)
        assert "strings" in response.json()["error"]
    response = create_token(base_url, admin_token, target={"type": "project", "projects": [project.lower(), project.upper()]})
    assert response.status_code == 400
    assert "duplicate" in response.json()["error"]

    response = create_token(base_url, admin_token, target={"type": "server", "projects": None})
    assert response.status_code == 400
    response = create_token(base_url, admin_token, target={"type": "server", "group": None})
    assert response.status_code == 400
    response = create_token(base_url, admin_token, target={"type": "group", "group": "g" * 65})
    assert response.status_code == 400
    assert issue(base_url, admin_token, target={"type": "group", "group": "g" * 64})["target"]["group"] == "g" * 64


# --- storage ----------------------------------------------------------------------------------------------------------


def test_storage_holds_only_the_hash(base_url, admin_token, mongo):
    project = new_project(base_url, admin_token)["@id"]
    issued = {
        "project": issue(base_url, admin_token, target={"type": "project", "projects": [project]}),
        "group": issue(base_url, admin_token, target={"type": "group", "group": "storage.group"}, scope="read"),
        "server": issue(base_url, admin_token),
    }
    # use one of them so that lastUsedAt moves in memory; the stored documents must stay free of the plaintext anyway
    assert status(base_url, issued["server"]["token"]) == 200

    collection = mongo[TEST_DB].api_tokens
    # no database write per request: the stored lastUsedAt is still 0 right after the use
    assert collection.find_one({"_id": issued["server"]["id"]})["lastUsedAt"] == 0
    # the plaintext is nowhere in the database, in no collection
    everything = " ".join(str(document) for name in mongo[TEST_DB].list_collection_names() for document in mongo[TEST_DB][name].find({}))
    assert issued["server"]["id"] in everything, "the search must really cover the token documents"
    for data in issued.values():
        assert data["token"] not in everything
        assert data["token"][4:] not in everything

    for kind, data in issued.items():
        document = collection.find_one({"_id": data["id"]})
        assert document is not None, kind
        assert document["tokenHash"] == sha256_hex(data["token"])
        assert document["name"] == data["name"]
        assert document["scope"] == data["scope"]
        assert document["createdBy"] == "admin"
        for field in ("createdAt", "expiresAt", "lastUsedAt"):
            assert isinstance(document[field], Int64), (kind, field, type(document[field]))
        assert document["createdAt"] == data["createdAt"]
        assert document["expiresAt"] == data["expiresAt"]
        assert document["targetType"] == kind
    assert collection.find_one({"_id": issued["project"]["id"]})["projects"] == [project]
    assert "group" not in collection.find_one({"_id": issued["project"]["id"]})
    assert collection.find_one({"_id": issued["group"]["id"]})["group"] == "storage.group"
    assert "projects" not in collection.find_one({"_id": issued["group"]["id"]})
    server_document = collection.find_one({"_id": issued["server"]["id"]})
    assert "projects" not in server_document and "group" not in server_document


# --- who may manage tokens ----------------------------------------------------------------------------------------------


@pytest.mark.parametrize("role", [0, 1, 2])
def test_non_admin_is_forbidden(base_url, admin_token, make_login, server_token, role):
    token_id, _ = server_token
    _, session = make_login(role=role)
    headers = auth(session)
    before = {token["id"] for token in list_tokens(base_url, admin_token)}
    body = {"name": f"forbidden-{role}", "target": {"type": "server"}, "scope": "read"}
    responses = [
        requests.post(f"{base_url}/admin/tokens", json=body, headers=headers, timeout=10),
        requests.get(f"{base_url}/admin/tokens", headers=headers, timeout=10),
        requests.get(f"{base_url}/admin/tokens/{token_id}", headers=headers, timeout=10),
        requests.delete(f"{base_url}/admin/tokens/{token_id}", headers=headers, timeout=10),
    ]
    for response in responses:
        assert response.status_code == 403
        assert response.json() == {"error": "Insufficient permissions."}
    # nothing was created or revoked
    assert get_token_detail(base_url, admin_token, token_id).status_code == 200
    assert {token["id"] for token in list_tokens(base_url, admin_token)} == before


def test_api_token_cannot_manage_tokens(base_url, admin_token, server_token):
    token_id, plaintext = server_token
    other = issue(base_url, admin_token)
    before = {token["id"] for token in list_tokens(base_url, admin_token)}
    body = {"name": "by-token", "target": {"type": "server"}, "scope": "read"}
    responses = [
        requests.post(f"{base_url}/admin/tokens", json=body, headers=auth(plaintext), timeout=10),
        requests.get(f"{base_url}/admin/tokens", headers=auth(plaintext), timeout=10),
        requests.get(f"{base_url}/admin/tokens/{other['id']}", headers=auth(plaintext), timeout=10),
        requests.delete(f"{base_url}/admin/tokens/{other['id']}", headers=auth(plaintext), timeout=10),
        requests.delete(f"{base_url}/admin/tokens/{token_id}", headers=auth(plaintext), timeout=10),
    ]
    for response in responses:
        assert response.status_code == 403
        assert response.json() == {"error": "session_required"}
    assert {token["id"] for token in list_tokens(base_url, admin_token)} == before
    assert status(base_url, plaintext) == 200
    assert status(base_url, other["token"]) == 200


def test_admin_with_pending_password_change_is_forbidden(base_url, admin_token, server_token, make_admin):
    token_id, _ = server_token
    name, _ = make_admin()
    reset = requests.put(f"{base_url}/admin/users/{name}", json={"password": "ResetByAdmin123!"}, headers=auth(admin_token), timeout=10)
    assert reset.status_code == 200
    session = login(base_url, name, "ResetByAdmin123!")
    assert requests.get(f"{base_url}/me", headers=auth(session), timeout=10).json()["mustChangePassword"] is True

    before = {token["id"] for token in list_tokens(base_url, admin_token)}
    body = {"name": "pending-change", "target": {"type": "server"}, "scope": "read"}
    responses = [
        requests.post(f"{base_url}/admin/tokens", json=body, headers=auth(session), timeout=10),
        requests.get(f"{base_url}/admin/tokens", headers=auth(session), timeout=10),
        requests.get(f"{base_url}/admin/tokens/{token_id}", headers=auth(session), timeout=10),
        requests.delete(f"{base_url}/admin/tokens/{token_id}", headers=auth(session), timeout=10),
    ]
    for response in responses:
        assert response.status_code == 403
        assert response.json() == {"error": "password_change_required"}
    assert {token["id"] for token in list_tokens(base_url, admin_token)} == before


# --- tampering --------------------------------------------------------------------------------------------------------


def test_manipulated_and_unknown_tokens_are_401(base_url, server_token):
    _, plaintext = server_token
    assert status(base_url, plaintext) == 200
    flipped = plaintext[:-1] + ("A" if plaintext[-1] != "A" else "B")
    first = ("A" if plaintext[4] != "A" else "B")
    candidates = {
        "last character changed": flipped,
        "first character changed": plaintext[:4] + first + plaintext[5:],
        "never issued, right length": "sst_" + "A" * 43,
        "prefix only": "sst_",
        "trailing characters": plaintext + "x",
        "one character short": plaintext[:-1],
        "upper case": plaintext.upper(),
        "wrong prefix": "sss_" + plaintext[4:],
    }
    assert len(candidates["never issued, right length"]) == 47
    for label, value in candidates.items():
        assert status(base_url, value) == 401, label
        assert status(base_url, value, "/me") == 401, label
    # the real one is unaffected
    assert status(base_url, plaintext) == 200


# --- pagination -------------------------------------------------------------------------------------------------------


def test_listing_is_paginated_with_stable_order(base_url, admin_token):
    for _ in range(5):
        issue(base_url, admin_token)
    headers = auth(admin_token)
    everything = list_tokens(base_url, admin_token)
    assert len(everything) >= 5

    first = requests.get(f"{base_url}/admin/tokens", params={"page[size]": 2}, headers=headers, timeout=10)
    assert first.status_code == 200
    assert len(first.json()) == 2
    assert "next" in first.links
    assert "prev" not in first.links

    def walk():
        pages, response = [], first
        while True:
            pages.append(response.json())
            link = response.links.get("next")
            if not link:
                return pages, response
            response = requests.get(link["url"], headers=headers, timeout=10)
            assert response.status_code == 200

    pages, last = walk()
    again, _ = walk()
    assert pages == again
    assert all(len(page) <= 2 for page in pages)
    assert "next" not in last.links
    walked = [item for page in pages for item in page]
    assert walked == everything
    ids = [item["id"] for item in walked]
    assert len(ids) == len(set(ids))
    assert [item["createdAt"] for item in walked] == sorted(item["createdAt"] for item in walked)

    assert get_all(f"{base_url}/admin/tokens", headers, **{"page[size]": 1}) == everything
    assert requests.get(f"{base_url}/admin/tokens", params={"page[size]": 0}, headers=headers, timeout=10).status_code == 400


# --- restart ----------------------------------------------------------------------------------------------------------


def test_token_survives_restart(mongo):
    config = {"login_max_requests_per_ip": 100000, "login_max_failures_per_user": 1000}
    base = f"http://127.0.0.1:{RESTART_PORT}"
    with start_server(RESTART_DB, RESTART_PORT, custom_config=config):
        admin = login(base, "admin", ADMIN_PASSWORD)
        project = new_project(base, admin, "restart")["@id"]
        response = create_token(base, admin, name="restart-token", target={"type": "project", "projects": [project]}, scope="read")
        assert response.status_code == 201, describe(response)
        created = response.json()
        plaintext = created["token"]
        assert requests.get(f"{base}/projects/{project}", headers=auth(plaintext), timeout=10).status_code == 200
        before = get_token_detail(base, admin, created["id"]).json()
        assert before["lastUsedAt"] > 0
        # lastUsedAt is written to the database at the orderly shutdown

    stored = mongo[RESTART_DB].api_tokens.find_one({"_id": created["id"]})
    assert stored is not None
    assert stored["lastUsedAt"] == before["lastUsedAt"]

    with start_server(RESTART_DB, RESTART_PORT, custom_config=config, drop_db=False):
        # first look at everything that came from the database, then use the token
        assert requests.get(f"{base}/admin/tokens", headers=auth(admin), timeout=10).status_code == 401
        assert requests.get(f"{base}/me", headers=auth(admin), timeout=10).status_code == 401, "login sessions do not survive a restart"
        admin = login(base, "admin", ADMIN_PASSWORD)
        detail = get_token_detail(base, admin, created["id"])
        assert detail.status_code == 200
        loaded = detail.json()
        for key in ("id", "name", "target", "scope", "createdBy", "createdAt", "expiresAt"):
            assert loaded[key] == created[key], key
        assert loaded["lastUsedAt"] == before["lastUsedAt"]
        assert "token" not in loaded
        assert plaintext not in detail.text

        assert requests.get(f"{base}/projects/{project}", headers=auth(plaintext), timeout=10).status_code == 200
        assert requests.post(f"{base}/projects/{project}/commits", json={}, headers=auth(plaintext), timeout=10).status_code == 403
        used = get_token_detail(base, admin, created["id"]).json()
        assert used["lastUsedAt"] >= before["lastUsedAt"]


def test_unloadable_token_document_is_reported_and_blocks_its_name(mongo):
    config = {"login_max_requests_per_ip": 100000, "login_max_failures_per_user": 1000}
    base = f"http://127.0.0.1:{BROKEN_PORT}"
    broken_id = str(uuid.uuid4())
    broken_name = f"broken-{uuid.uuid4().hex[:8]}"
    fake_hash = uuid.uuid4().hex + uuid.uuid4().hex
    with start_server(BROKEN_DB, BROKEN_PORT, custom_config=config):
        pass  # creates the database, schema and indexes
    collection = mongo[BROKEN_DB].api_tokens
    try:
        # a group token without a group name can not be parsed
        collection.insert_one({"_id": broken_id, "tokenHash": fake_hash, "name": broken_name, "targetType": "group", "group": "",
                               "scope": "read", "createdBy": "admin", "createdAt": Int64(1), "expiresAt": Int64(2 ** 40), "lastUsedAt": Int64(0)})
        with start_server(BROKEN_DB, BROKEN_PORT, custom_config=config, drop_db=False) as (_, output):
            admin = login(base, "admin", ADMIN_PASSWORD)
            text = output.read_text(errors="replace")
            assert broken_id in text
            assert "1 stored API token document(s) could not be loaded" in text
            assert fake_hash not in text

            assert broken_id not in [token["id"] for token in list_tokens(base, admin)]
            response = create_token(base, admin, name=broken_name)
            assert response.status_code == 409, describe(response)
            assert "could not be loaded" in response.json()["error"]
            assert fake_hash not in output.read_text(errors="replace")
    finally:
        collection.delete_one({"_id": broken_id})
        mongo.drop_database(BROKEN_DB)
