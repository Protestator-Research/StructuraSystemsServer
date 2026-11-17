"""API tokens, what a token may do: the rights matrix per target type and scope, session-only areas, lifecycle."""
import uuid
from types import SimpleNamespace

import pytest
import requests

from conftest import LIBRARY_DESCRIPTION, TEST_DB, USER_PASSWORD, auth, get_all, login
from token_helpers import (make_admin, SERVER_TARGET, commit_body, describe, get_token_detail, issue, list_tokens, new_commit, new_project,
                           remove_all_tokens, status)

TARGETS = ("project", "group", "server")
SCOPES = ("read", "read_write")
COMBOS = [(target, scope) for target in TARGETS for scope in SCOPES]
INSUFFICIENT_SCOPE = {"error": "insufficient_scope"}
SESSION_REQUIRED = {"error": "session_required"}


@pytest.fixture(scope="module", autouse=True)
def remove_tokens_afterwards(base_url, admin_token):
    yield
    remove_all_tokens(base_url, admin_token)


@pytest.fixture(scope="module")
def world(base_url, admin_token):
    """Projects and one token per target type and scope.

    p1, p2: the projects of the project tokens; g1, g2: owned by the group of the group tokens; other: owned by `admin`, no group
    (a foreign project for project and group tokens); other_group: owned by a different group.
    """
    w = SimpleNamespace()
    w.group = f"grp{uuid.uuid4().hex[:8]}"
    w.p1, w.p2, w.other = (new_project(base_url, admin_token)["@id"] for _ in range(3))
    w.g1, w.g2 = (new_project(base_url, admin_token, owningGroup=w.group)["@id"] for _ in range(2))
    w.other_group = new_project(base_url, admin_token, owningGroup=f"foreign{uuid.uuid4().hex[:6]}")["@id"]
    w.commits = {pid: new_commit(base_url, admin_token, pid) for pid in (w.p1, w.p2, w.g1, w.g2, w.other, w.other_group)}
    targets = {"project": {"type": "project", "projects": [w.p1, w.p2]}, "group": {"type": "group", "group": w.group}, "server": SERVER_TARGET}
    w.tokens = {(target, scope): issue(base_url, admin_token, target=body, scope=scope)["token"]
                for target, body in targets.items() for scope in SCOPES}
    # the project (or group) projects a token of that target type may use, and foreign ones it may not
    w.allowed = {"project": [w.p1, w.p2], "group": [w.g1, w.g2], "server": [w.p1, w.g1, w.other]}
    w.foreign = {"project": [w.other, w.g1, w.other_group], "group": [w.other, w.p1, w.other_group], "server": []}
    return w


def headers_of(world, combo):
    return auth(world.tokens[combo])


def project_ids(base_url, plaintext):
    return {project["@id"] for project in get_all(f"{base_url}/projects", auth(plaintext))}


def put_project(base_url, headers, project_id):
    return requests.put(f"{base_url}/projects/{project_id}", json={"name": f"renamed {uuid.uuid4().hex[:6]}"}, headers=headers, timeout=10)


def post_branch(base_url, headers, project_id, head):
    return requests.post(f"{base_url}/projects/{project_id}/branches", json={"@type": "Branch", "name": f"b{uuid.uuid4().hex[:8]}", "head": {"@id": head}},
                         headers=headers, timeout=10)


def post_tag(base_url, headers, project_id, commit):
    return requests.post(f"{base_url}/projects/{project_id}/tags", json={"name": f"t{uuid.uuid4().hex[:8]}", "taggedCommit": {"@id": commit}},
                         headers=headers, timeout=10)


def post_stored_query(base_url, headers, project_id):
    return requests.post(f"{base_url}/projects/{project_id}/queries", json={"@type": "Query", "name": "q"}, headers=headers, timeout=10)


def post_query_results(base_url, headers, project_id, commit):
    return requests.post(f"{base_url}/projects/{project_id}/query-results", json={"@type": "Query"}, params={"commitId": commit}, headers=headers, timeout=10)


# --- reading, listing -------------------------------------------------------------------------------------------------


@pytest.mark.parametrize("combo", COMBOS, ids=str)
def test_allowed_projects_are_readable(base_url, world, combo):
    headers = headers_of(world, combo)
    for pid in world.allowed[combo[0]]:
        assert requests.get(f"{base_url}/projects/{pid}", headers=headers, timeout=10).status_code == 200, pid
        assert requests.get(f"{base_url}/projects/{pid}/commits", headers=headers, timeout=10).status_code == 200, pid
        assert requests.get(f"{base_url}/projects/{pid}/branches", headers=headers, timeout=10).status_code == 200, pid
        assert requests.get(f"{base_url}/projects/{pid}/commits/{world.commits[pid]}", headers=headers, timeout=10).status_code == 200, pid


@pytest.mark.parametrize("combo", [c for c in COMBOS if c[0] != "server"], ids=str)
def test_foreign_projects_are_404_everywhere(base_url, world, combo):
    headers = headers_of(world, combo)
    for pid in world.foreign[combo[0]]:
        for path in ("", "/commits", "/branches", "/tags", f"/commits/{world.commits[pid]}", f"/commits/{world.commits[pid]}/changes"):
            response = requests.get(f"{base_url}/projects/{pid}{path}", headers=headers, timeout=10)
            assert response.status_code == 404, (pid, path)
        # the same answer as for a project that does not exist
        unknown = requests.get(f"{base_url}/projects/{uuid.uuid4()}", headers=headers, timeout=10)
        assert unknown.status_code == 404
        assert requests.get(f"{base_url}/projects/{pid}", headers=headers, timeout=10).text == unknown.text


@pytest.mark.parametrize("scope", SCOPES)
def test_project_list_is_exactly_the_permitted_set(base_url, admin_token, mongo, world, scope):
    assert project_ids(base_url, world.tokens[("project", scope)]) == {world.p1, world.p2}
    in_group = {d["_id"] for d in mongo[TEST_DB].projects.find({"owningGroup": world.group})}
    assert {world.g1, world.g2} <= in_group
    assert project_ids(base_url, world.tokens[("group", scope)]) == in_group
    # the server token sees everything an administrator sees, library projects included
    assert project_ids(base_url, world.tokens[("server", scope)]) == project_ids(base_url, admin_token)


# --- read scope ---------------------------------------------------------------------------------------------------------


@pytest.mark.parametrize("target", TARGETS)
def test_read_scope_blocks_every_writing_method(base_url, world, target):
    headers = headers_of(world, (target, "read"))
    for pid in world.allowed[target] + world.foreign[target][:1]:
        commit = world.commits[pid]
        responses = {
            "commit": requests.post(f"{base_url}/projects/{pid}/commits", json=commit_body(), headers=headers, timeout=10),
            "branch": post_branch(base_url, headers, pid, commit),
            "tag": post_tag(base_url, headers, pid, commit),
            "put project": put_project(base_url, headers, pid),
            "query": post_stored_query(base_url, headers, pid),
            "delete project": requests.delete(f"{base_url}/projects/{pid}", headers=headers, timeout=10),
        }
        for label, response in responses.items():
            assert response.status_code == 403, (pid, label)
            assert response.json() == INSUFFICIENT_SCOPE, (pid, label)
    created = requests.post(f"{base_url}/projects", json={"name": "nope"}, headers=headers, timeout=10)
    assert created.status_code == 403
    assert created.json() == INSUFFICIENT_SCOPE


@pytest.mark.parametrize("target", TARGETS)
def test_read_scope_allows_head(base_url, world, target):
    headers = headers_of(world, (target, "read"))
    pid = world.allowed[target][0]
    response = requests.head(f"{base_url}/projects/{pid}", headers=headers, timeout=10)
    assert response.status_code == 200, describe(response)
    # the scope check does not change the visibility: a foreign project is not found (the server token has no foreign project)
    if world.foreign[target]:
        foreign = requests.head(f"{base_url}/projects/{world.foreign[target][0]}", headers=headers, timeout=10)
        assert foreign.status_code == 404, describe(foreign)


@pytest.mark.parametrize("target", TARGETS)
def test_read_scope_may_run_queries(base_url, world, target):
    headers = headers_of(world, (target, "read"))
    pid = world.allowed[target][0]
    response = post_query_results(base_url, headers, pid, world.commits[pid])
    assert response.status_code == 200, describe(response)
    assert isinstance(response.json(), list)
    assert requests.get(f"{base_url}/projects/{pid}/query-results", params={"commitId": world.commits[pid]}, headers=headers, timeout=10).status_code in (200, 400)
    for foreign in world.foreign[target][:1]:
        assert post_query_results(base_url, headers, foreign, world.commits[foreign]).status_code == 404
    # writing a stored query is not reading
    assert post_stored_query(base_url, headers, pid).status_code == 403


@pytest.mark.parametrize("target", TARGETS)
def test_read_write_scope_may_write_allowed_projects(base_url, world, target):
    headers = headers_of(world, (target, "read_write"))
    pid = world.allowed[target][0]
    commit = requests.post(f"{base_url}/projects/{pid}/commits", json=commit_body("by token"), headers=headers, timeout=10)
    assert commit.status_code == 201, describe(commit)
    head = commit.json()["@id"]
    assert post_branch(base_url, headers, pid, head).status_code == 201
    assert post_tag(base_url, headers, pid, head).status_code == 201
    assert put_project(base_url, headers, pid).status_code == 200
    assert post_stored_query(base_url, headers, pid).status_code == 201
    assert post_query_results(base_url, headers, pid, head).status_code == 200


@pytest.mark.parametrize("target", ["project", "group"])
def test_read_write_scope_cannot_write_foreign_projects(base_url, world, target):
    headers = headers_of(world, (target, "read_write"))
    for pid in world.foreign[target]:
        commit = world.commits[pid]
        responses = [
            requests.post(f"{base_url}/projects/{pid}/commits", json=commit_body(), headers=headers, timeout=10),
            post_branch(base_url, headers, pid, commit),
            post_tag(base_url, headers, pid, commit),
            put_project(base_url, headers, pid),
            post_stored_query(base_url, headers, pid),
            post_query_results(base_url, headers, pid, commit),
        ]
        assert [response.status_code for response in responses] == [404] * len(responses), pid


# --- creating and deleting projects -------------------------------------------------------------------------------------


@pytest.mark.parametrize("scope", SCOPES)
def test_project_token_cannot_create_projects(base_url, admin_token, world, scope):
    name = f"must-not-exist-{uuid.uuid4().hex[:8]}"
    response = requests.post(f"{base_url}/projects", json={"name": name}, headers=headers_of(world, ("project", scope)), timeout=10)
    assert response.status_code == 403
    assert name not in [p["name"] for p in get_all(f"{base_url}/projects", auth(admin_token))]


def test_group_token_creates_projects_of_its_group(base_url, admin_token, mongo, world):
    group = f"creator{uuid.uuid4().hex[:6]}"
    token = issue(base_url, admin_token, target={"type": "group", "group": group})["token"]
    # an owner and a group supplied by the client are ignored
    response = requests.post(f"{base_url}/projects?owner=admin&owningGroup=evil", json={"name": "by group token", "owningGroup": "evil", "owner": "admin"},
                             headers=auth(token), timeout=10)
    assert response.status_code == 201, describe(response)
    pid = response.json()["@id"]
    document = mongo[TEST_DB].projects.find_one({"_id": pid})
    assert document["owningGroup"] == group
    assert not document.get("owner")
    assert requests.get(f"{base_url}/projects/{pid}", headers=auth(token), timeout=10).status_code == 200
    assert pid in project_ids(base_url, token)
    assert requests.get(f"{base_url}/projects/{pid}", headers=auth(admin_token), timeout=10).status_code == 200
    # not visible to the tokens of other targets
    assert requests.get(f"{base_url}/projects/{pid}", headers=headers_of(world, ("group", "read_write")), timeout=10).status_code == 404
    assert requests.get(f"{base_url}/projects/{pid}", headers=headers_of(world, ("project", "read_write")), timeout=10).status_code == 404
    # a group token may not delete it (403, the project is readable)
    assert requests.delete(f"{base_url}/projects/{pid}", headers=auth(token), timeout=10).status_code == 403
    assert requests.get(f"{base_url}/projects/{pid}", headers=auth(admin_token), timeout=10).status_code == 200


def test_server_token_creates_projects_without_owner(base_url, admin_token, mongo, make_login, world):
    token = world.tokens[("server", "read_write")]
    response = requests.post(f"{base_url}/projects?owningGroup=evil&owner=admin", json={"name": "by server token", "owningGroup": "evil", "owner": "admin"},
                             headers=auth(token), timeout=10)
    assert response.status_code == 201, describe(response)
    pid = response.json()["@id"]
    document = mongo[TEST_DB].projects.find_one({"_id": pid})
    assert not document.get("owner")
    assert not document.get("owningGroup")
    assert requests.get(f"{base_url}/projects/{pid}", headers=auth(token), timeout=10).status_code == 200
    assert requests.get(f"{base_url}/projects/{pid}", headers=auth(admin_token), timeout=10).status_code == 200
    # a project without owner and group is for administrators and server tokens only
    for combo in (("group", "read_write"), ("project", "read_write")):
        assert requests.get(f"{base_url}/projects/{pid}", headers=headers_of(world, combo), timeout=10).status_code == 404
    _, user = make_login()
    assert requests.get(f"{base_url}/projects/{pid}", headers=auth(user), timeout=10).status_code == 404


@pytest.mark.parametrize("target", ["project", "group"])
def test_project_and_group_tokens_cannot_delete(base_url, admin_token, world, target):
    headers = headers_of(world, (target, "read_write"))
    for pid in world.allowed[target]:
        response = requests.delete(f"{base_url}/projects/{pid}", headers=headers, timeout=10)
        assert response.status_code == 403, pid
        assert requests.get(f"{base_url}/projects/{pid}", headers=auth(admin_token), timeout=10).status_code == 200
    for pid in world.foreign[target]:
        assert requests.delete(f"{base_url}/projects/{pid}", headers=headers, timeout=10).status_code == 404, pid
        assert requests.get(f"{base_url}/projects/{pid}", headers=auth(admin_token), timeout=10).status_code == 200


def test_server_token_deletes_projects_only_with_read_write(base_url, admin_token, world):
    pid = new_project(base_url, admin_token)["@id"]
    read = headers_of(world, ("server", "read"))
    assert requests.delete(f"{base_url}/projects/{pid}", headers=read, timeout=10).json() == INSUFFICIENT_SCOPE
    assert requests.get(f"{base_url}/projects/{pid}", headers=auth(admin_token), timeout=10).status_code == 200
    response = requests.delete(f"{base_url}/projects/{pid}", headers=headers_of(world, ("server", "read_write")), timeout=10)
    assert response.status_code == 200, describe(response)
    assert requests.get(f"{base_url}/projects/{pid}", headers=auth(admin_token), timeout=10).status_code == 404


# --- library projects ---------------------------------------------------------------------------------------------------


@pytest.mark.parametrize("scope", SCOPES)
def test_library_projects_are_for_server_tokens_only(base_url, admin_token, world, scope):
    libraries = [p for p in get_all(f"{base_url}/projects", auth(admin_token)) if p.get("description") == LIBRARY_DESCRIPTION]
    assert libraries, "the server fixture seeds a library project"
    library = libraries[0]["@id"]

    server = auth(world.tokens[("server", scope)])
    assert requests.get(f"{base_url}/projects/{library}", headers=server, timeout=10).status_code == 200
    assert requests.get(f"{base_url}/projects/{library}/commits", headers=server, timeout=10).status_code == 200
    assert library in project_ids(base_url, world.tokens[("server", scope)])

    for target in ("project", "group"):
        plaintext = world.tokens[(target, scope)]
        assert requests.get(f"{base_url}/projects/{library}", headers=auth(plaintext), timeout=10).status_code == 404
        assert requests.get(f"{base_url}/projects/{library}/commits", headers=auth(plaintext), timeout=10).status_code == 404
        ids = project_ids(base_url, plaintext)
        assert library not in ids
        assert not ({p["@id"] for p in libraries} & ids)


# --- session-only areas -------------------------------------------------------------------------------------------------


@pytest.mark.parametrize("scope", SCOPES)
@pytest.mark.parametrize("target", TARGETS)
def test_session_only_paths(base_url, world, target, scope):
    headers = headers_of(world, (target, scope))
    pid = world.p1
    requests_to_try = [
        ("GET", "/admin/users", None),
        ("POST", "/admin/users", {"username": "x", "password": USER_PASSWORD, "role": 0}),
        ("PUT", "/admin/users/nobody_here", {"role": 0}),
        ("GET", "/groups/anything/members", None),
        ("GET", "/me", None),
        ("PUT", "/me/password", {"oldPassword": "a", "newPassword": "b"}),
        ("PUT", f"/projects/{pid}/owner", {"owner": "admin"}),
    ]
    for method, path, body in requests_to_try:
        response = requests.request(method, f"{base_url}{path}", json=body, headers=headers, timeout=10)
        assert response.status_code == 403, (method, path)
        assert response.json() == SESSION_REQUIRED, (method, path)


@pytest.mark.parametrize("target", TARGETS)
def test_session_only_path_variants(base_url, world, target):
    headers = headers_of(world, (target, "read_write"))
    pid = world.p1

    def send(method, path, body=None):
        return requests.request(method, f"{base_url}{path}", json=body, headers=headers, timeout=10)

    for path in ("/ADMIN/users", "/Admin/users", "/GROUPS/x/members", "/ME"):
        response = send("GET", path)
        assert response.status_code == 403, path
        assert response.json() == SESSION_REQUIRED, path
    response = send("PUT", f"/projects/{pid}/OWNER", {"owner": "admin"})
    assert response.status_code == 403
    response = send("PUT", f"/projects/{pid}/Owner", {"owner": "admin"})
    assert response.status_code == 403
    response = send("PUT", f"/projects//owner", {"owner": "admin"})
    assert response.status_code == 403
    # not routed at all
    assert send("GET", "/admin//users").status_code == 404
    assert send("GET", "/me/").status_code == 404


# --- lifecycle ----------------------------------------------------------------------------------------------------------


def test_project_deletion_shrinks_and_removes_tokens(base_url, admin_token, mongo):
    first, second = new_project(base_url, admin_token)["@id"], new_project(base_url, admin_token)["@id"]
    data = issue(base_url, admin_token, target={"type": "project", "projects": [first, second]})
    plaintext = data["token"]
    assert status(base_url, plaintext, f"/projects/{first}") == 200

    assert requests.delete(f"{base_url}/projects/{first}", headers=auth(admin_token), timeout=10).status_code == 200
    detail = get_token_detail(base_url, admin_token, data["id"])
    assert detail.status_code == 200
    assert detail.json()["target"] == {"type": "project", "projects": [second]}
    assert mongo[TEST_DB].api_tokens.find_one({"_id": data["id"]})["projects"] == [second]
    assert status(base_url, plaintext, f"/projects/{first}") == 404
    assert status(base_url, plaintext, f"/projects/{second}") == 200
    assert project_ids(base_url, plaintext) == {second}

    assert requests.delete(f"{base_url}/projects/{second}", headers=auth(admin_token), timeout=10).status_code == 200
    assert get_token_detail(base_url, admin_token, data["id"]).status_code == 404
    assert mongo[TEST_DB].api_tokens.find_one({"_id": data["id"]}) is None
    assert data["id"] not in [token["id"] for token in list_tokens(base_url, admin_token)]
    assert status(base_url, plaintext) == 401


def test_deleting_a_project_through_a_server_token_updates_tokens_too(base_url, admin_token, mongo):
    pid = new_project(base_url, admin_token)["@id"]
    project_token = issue(base_url, admin_token, target={"type": "project", "projects": [pid]})
    server_token = issue(base_url, admin_token)["token"]
    assert requests.delete(f"{base_url}/projects/{pid}", headers=auth(server_token), timeout=10).status_code == 200
    assert get_token_detail(base_url, admin_token, project_token["id"]).status_code == 404
    assert mongo[TEST_DB].api_tokens.find_one({"_id": project_token["id"]}) is None
    assert status(base_url, project_token["token"]) == 401
    assert status(base_url, server_token) == 200


def test_token_outlives_its_deleted_issuing_administrator(base_url, admin_token, make_admin):
    name, password = make_admin()
    session = login(base_url, name, password)
    project = new_project(base_url, admin_token)["@id"]
    data = issue(base_url, session, target={"type": "project", "projects": [project]})
    assert data["createdBy"] == name

    response = requests.delete(f"{base_url}/admin/users/{name}", headers=auth(admin_token), timeout=10)
    assert response.status_code == 200, describe(response)
    assert requests.get(f"{base_url}/me", headers=auth(session), timeout=10).status_code == 401
    assert status(base_url, data["token"], f"/projects/{project}") == 200
    detail = get_token_detail(base_url, admin_token, data["id"])
    assert detail.status_code == 200
    assert detail.json()["createdBy"] == name
    # any administrator can revoke it
    assert requests.delete(f"{base_url}/admin/tokens/{data['id']}", headers=auth(admin_token), timeout=10).status_code == 204


def test_token_outlives_demotion_and_password_change_of_its_issuing_administrator(base_url, admin_token, make_admin):
    name, password = make_admin()
    session = login(base_url, name, password)
    data = issue(base_url, session)
    plaintext = data["token"]
    assert status(base_url, plaintext) == 200

    demoted = requests.put(f"{base_url}/admin/users/{name}", json={"role": 0}, headers=auth(admin_token), timeout=10)
    assert demoted.status_code == 200, describe(demoted)
    now_user = login(base_url, name, password)
    assert requests.get(f"{base_url}/admin/tokens", headers=auth(now_user), timeout=10).status_code == 403, "no administrator any more"
    assert status(base_url, plaintext) == 200

    reset = requests.put(f"{base_url}/admin/users/{name}", json={"password": "ResetByAdmin123!"}, headers=auth(admin_token), timeout=10)
    assert reset.status_code == 200
    assert status(base_url, plaintext) == 200
    detail = get_token_detail(base_url, admin_token, data["id"])
    assert detail.status_code == 200
    assert detail.json()["createdBy"] == name


def test_group_token_works_without_group_members(base_url, admin_token):
    group = f"empty{uuid.uuid4().hex[:8]}"
    pid = new_project(base_url, admin_token, owningGroup=group)["@id"]
    assert group not in [user.get("group") for user in get_all(f"{base_url}/admin/users", auth(admin_token))]
    token = issue(base_url, admin_token, target={"type": "group", "group": group})["token"]
    assert status(base_url, token, f"/projects/{pid}") == 200
    assert project_ids(base_url, token) == {pid}


def test_revoked_token_is_rejected_on_every_route(base_url, admin_token):
    data = issue(base_url, admin_token)
    pid = new_project(base_url, admin_token)["@id"]
    assert status(base_url, data["token"], f"/projects/{pid}") == 200
    assert requests.delete(f"{base_url}/admin/tokens/{data['id']}", headers=auth(admin_token), timeout=10).status_code == 204
    for path in ("/projects", f"/projects/{pid}", f"/projects/{pid}/commits", "/me", "/admin/tokens"):
        assert status(base_url, data["token"], path) == 401, path
