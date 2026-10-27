import uuid

import pytest
import requests

from conftest import auth, get_all


def create_project(base_url, token, **extra):
    response = requests.post(
        f"{base_url}/projects",
        json={"name": f"authz_{uuid.uuid4().hex[:8]}", "description": "authz", **extra},
        headers=auth(token),
        timeout=10,
    )
    return response


def project_paths(project):
    pid = project["@id"]
    branch = project["defaultBranch"]["@id"]
    return [
        f"/projects/{pid}",
        f"/projects/{pid}/commits",
        f"/projects/{pid}/branches",
        f"/projects/{pid}/branches/{branch}",
        f"/projects/{pid}/tags",
        f"/projects/{pid}/commits/{uuid.uuid4()}",
        f"/projects/{pid}/commits/{uuid.uuid4()}/changes",
        f"/projects/{pid}/commits/{uuid.uuid4()}/elements",
        f"/projects/{pid}/twins",
        f"/project/{pid}/twins/{uuid.uuid4()}/data",
    ]


@pytest.fixture
def owner(make_login):
    return make_login()


@pytest.fixture
def project(base_url, owner):
    response = create_project(base_url, owner[1])
    assert response.status_code == 201, response.text
    return response.json()


def test_owner_has_access(base_url, owner, project):
    pid = project["@id"]
    token = owner[1]
    assert requests.get(f"{base_url}/projects/{pid}", headers=auth(token), timeout=10).status_code == 200
    assert requests.get(f"{base_url}/projects/{pid}/commits", headers=auth(token), timeout=10).status_code == 200
    assert requests.get(f"{base_url}/projects/{pid}/twins", headers=auth(token), timeout=10).status_code == 200


def test_foreign_user_gets_404_everywhere(base_url, make_login, project):
    _, other = make_login()
    pid = project["@id"]
    branch = project["defaultBranch"]["@id"]
    for path in project_paths(project):
        response = requests.get(f"{base_url}{path}", headers=auth(other), timeout=10)
        assert response.status_code == 404, f"GET {path}: {response.status_code}"

    commit_body = {"description": "x", "change": []}
    branch_body = {"name": "b", "head": {"@id": str(uuid.uuid4())}}
    twin_body = {"name": "t", "referencedCommit": {"@id": str(uuid.uuid4())}}
    for path, body in [(f"/projects/{pid}/commits", commit_body),
                       (f"/projects/{pid}/branches", branch_body),
                       (f"/projects/{pid}/twins", twin_body)]:
        response = requests.post(f"{base_url}{path}", json=body, headers=auth(other), timeout=10)
        assert response.status_code == 404, f"POST {path}: {response.status_code}"

    for path in [f"/projects/{pid}/branches/{branch}", f"/projects/{pid}"]:
        response = requests.delete(f"{base_url}{path}", headers=auth(other), timeout=10)
        assert response.status_code == 404, f"DELETE {path}: {response.status_code}"


def test_denied_and_unknown_look_identical(base_url, make_login, project):
    _, other = make_login()
    denied = requests.get(f"{base_url}/projects/{project['@id']}", headers=auth(other), timeout=10)
    unknown = requests.get(f"{base_url}/projects/{uuid.uuid4()}", headers=auth(other), timeout=10)
    assert denied.status_code == unknown.status_code == 404
    assert denied.text == unknown.text


def test_foreign_project_not_listed(base_url, make_login, project):
    _, other = make_login()
    response = requests.get(f"{base_url}/projects", headers=auth(other), timeout=30)
    assert response.status_code == 200
    assert project["@id"] not in [p["@id"] for p in response.json()]


def test_project_survives_foreign_delete(base_url, owner, make_login, project):
    _, other = make_login()
    requests.delete(f"{base_url}/projects/{project['@id']}", headers=auth(other), timeout=10)
    assert requests.get(f"{base_url}/projects/{project['@id']}", headers=auth(owner[1]), timeout=10).status_code == 200


def test_admin_sees_everything(base_url, admin_token, project):
    assert requests.get(f"{base_url}/projects/{project['@id']}", headers=auth(admin_token), timeout=10).status_code == 200
    listed = get_all(f"{base_url}/projects", auth(admin_token))
    assert project["@id"] in [p["@id"] for p in listed]


def test_group_access(base_url, make_login):
    group = f"team{uuid.uuid4().hex[:6]}"
    _, token_a = make_login(group=group)
    _, token_c = make_login(group=group)
    _, token_d = make_login(group=f"other{uuid.uuid4().hex[:6]}")

    response = create_project(base_url, token_a, owningGroup=group)
    assert response.status_code == 201, response.text
    project = response.json()
    pid = project["@id"]

    # Member C: read and write, but no delete.
    assert requests.get(f"{base_url}/projects/{pid}", headers=auth(token_c), timeout=10).status_code == 200
    listed = get_all(f"{base_url}/projects", auth(token_c))
    assert pid in [p["@id"] for p in listed]
    commit = requests.post(f"{base_url}/projects/{pid}/commits",
                           json={"description": "by C", "change": [{"payload": {
                               "@type": "Package", "@id": str(uuid.uuid4()), "declaredName": "by C"}}]},
                           headers=auth(token_c), timeout=10)
    assert commit.status_code == 201, commit.text
    assert requests.delete(f"{base_url}/projects/{pid}", headers=auth(token_c), timeout=10).status_code == 404

    # Member of another group: nothing.
    assert requests.get(f"{base_url}/projects/{pid}", headers=auth(token_d), timeout=10).status_code == 404
    listed = get_all(f"{base_url}/projects", auth(token_d))
    assert pid not in [p["@id"] for p in listed]

    # The owner may delete.
    assert requests.delete(f"{base_url}/projects/{pid}", headers=auth(token_a), timeout=10).status_code == 200


def test_cannot_create_project_for_foreign_group(base_url, make_login):
    _, token = make_login(group="mine")
    assert create_project(base_url, token, owningGroup="notmine").status_code == 403
    _, groupless = make_login()
    assert create_project(base_url, groupless, owningGroup="mine").status_code == 403


def test_admin_may_create_for_any_group(base_url, admin_token):
    response = create_project(base_url, admin_token, owningGroup="anygroup")
    assert response.status_code == 201


def test_library_projects_admin_only(base_url, admin_token, make_login):
    admin_list = get_all(f"{base_url}/projects", auth(admin_token))
    libraries = [p for p in admin_list if p.get("description") == "Preloaded Project"]
    assert libraries, "fresh DB should preload library projects"
    library_id = libraries[0]["@id"]

    _, token = make_login()
    user_list = get_all(f"{base_url}/projects", auth(token))
    assert all(p.get("description") != "Preloaded Project" for p in user_list)
    assert library_id not in [p["@id"] for p in user_list]
    assert requests.get(f"{base_url}/projects/{library_id}", headers=auth(token), timeout=10).status_code == 404
    assert requests.get(f"{base_url}/projects/{library_id}/commits", headers=auth(token), timeout=10).status_code == 404
    assert requests.get(f"{base_url}/projects/{library_id}", headers=auth(admin_token), timeout=10).status_code == 200


def test_moderator_has_no_project_access(base_url, make_login, project):
    _, moderator = make_login(role=2)
    assert requests.get(f"{base_url}/projects/{project['@id']}", headers=auth(moderator), timeout=10).status_code == 404


def test_owner_query_parameter_is_ignored(base_url, owner, make_login):
    other_name, _ = make_login()
    response = requests.post(
        f"{base_url}/projects?owner={other_name}",
        json={"name": "ownerparam", "description": "x"},
        headers=auth(owner[1]),
        timeout=10,
    )
    assert response.status_code == 201
    pid = response.json()["@id"]
    assert requests.get(f"{base_url}/projects/{pid}", headers=auth(owner[1]), timeout=10).status_code == 200
    _, third = make_login()
    assert requests.get(f"{base_url}/projects/{pid}", headers=auth(third), timeout=10).status_code == 404


def test_owner_change(base_url, admin_token, owner, make_login, mongo):
    from conftest import TEST_DB

    new_name, new_token = make_login()
    _, stranger = make_login()
    pid = create_project(base_url, owner[1]).json()["@id"]
    url = f"{base_url}/projects/{pid}/owner"

    # A non-owner cannot even see the project.
    assert requests.put(url, json={"owner": new_name}, headers=auth(stranger), timeout=10).status_code == 404
    # Unknown new owner.
    assert requests.put(url, json={"owner": "nobody_here"}, headers=auth(admin_token), timeout=10).status_code == 400
    assert requests.put(url, json={}, headers=auth(admin_token), timeout=10).status_code == 400

    response = requests.put(url, json={"owner": new_name}, headers=auth(admin_token), timeout=10)
    assert response.status_code == 200, response.text
    assert mongo[TEST_DB].projects.find_one({"_id": pid})["owner"] == new_name
    assert requests.get(f"{base_url}/projects/{pid}", headers=auth(new_token), timeout=10).status_code == 200
    assert requests.get(f"{base_url}/projects/{pid}", headers=auth(owner[1]), timeout=10).status_code == 404

    # The new owner can pass it on and set a group they belong to; clearing persists.
    other_name, _ = make_login()
    assert requests.put(url, json={"owner": other_name}, headers=auth(new_token), timeout=10).status_code == 200
    assert requests.put(url, json={"owningGroup": "foreign"}, headers=auth(admin_token), timeout=10).status_code == 200
    assert mongo[TEST_DB].projects.find_one({"_id": pid})["owningGroup"] == "foreign"
    assert requests.put(url, json={"owningGroup": ""}, headers=auth(admin_token), timeout=10).status_code == 200
    assert "owningGroup" not in mongo[TEST_DB].projects.find_one({"_id": pid})


def test_owner_can_reassign_own_project(base_url, owner, make_login):
    new_name, new_token = make_login()
    pid = create_project(base_url, owner[1]).json()["@id"]
    response = requests.put(f"{base_url}/projects/{pid}/owner", json={"owner": new_name},
                            headers=auth(owner[1]), timeout=10)
    assert response.status_code == 200
    assert requests.get(f"{base_url}/projects/{pid}", headers=auth(new_token), timeout=10).status_code == 200
