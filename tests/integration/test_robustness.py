import uuid

import pytest
import requests

from conftest import auth


@pytest.fixture
def headers(admin_token):
    return auth(admin_token)


@pytest.fixture
def project(base_url, headers):
    response = requests.post(
        f"{base_url}/projects",
        json={"name": f"robust_{uuid.uuid4().hex[:8]}", "description": "robustness"},
        headers=headers,
        timeout=10,
    )
    assert response.status_code == 201, response.text
    return response.json()


def assert_alive(base_url, headers):
    response = requests.get(f"{base_url}/projects", headers=headers, timeout=10)
    assert response.status_code == 200


def post_paths(project_id):
    return [
        "/projects",
        f"/projects/{project_id}/commits",
        f"/projects/{project_id}/branches",
        f"/projects/{project_id}/twins",
        "/admin/users",
        "/login",
    ]


@pytest.mark.parametrize("index", range(6))
@pytest.mark.parametrize("body", [None, "{bad json"], ids=["no_body", "bad_json"])
def test_post_with_invalid_body_returns_400(base_url, headers, project, index, body):
    path = post_paths(project["@id"])[index]
    kwargs = {}
    if body is not None:
        kwargs["data"] = body
        kwargs["headers"] = {**headers, "Content-Type": "application/json"}
    else:
        kwargs["headers"] = headers
    response = requests.post(f"{base_url}{path}", timeout=10, **kwargs)
    assert response.status_code == 400, f"{path}: {response.status_code} {response.text}"
    assert_alive(base_url, headers)


def test_post_with_wrongly_typed_body_does_not_crash(base_url, headers, project):
    for path in post_paths(project["@id"]):
        response = requests.post(f"{base_url}{path}", json=[1, 2, 3], headers=headers, timeout=10)
        assert response.status_code < 500, f"{path}: {response.status_code}"
        assert_alive(base_url, headers)


def test_invalid_uuids_return_400(base_url, headers, project):
    pid = project["@id"]
    paths = [
        "/projects/not-a-uuid",
        f"/projects/{pid}/commits/xyz",
        f"/projects/{pid}/commits/xyz/changes",
        f"/projects/{pid}/commits/xyz/elements",
        f"/projects/{pid}/branches/xyz",
        f"/projects/{pid}/tags/xyz",
        "/projects/not-a-uuid/commits",
        "/projects/not-a-uuid/branches",
        "/projects/not-a-uuid/tags",
        "/projects/not-a-uuid/twins",
    ]
    for path in paths:
        response = requests.get(f"{base_url}{path}", headers=headers, timeout=10)
        assert response.status_code == 400, f"GET {path}: {response.status_code}"
    for path in ["/projects/not-a-uuid", f"/projects/{pid}/branches/xyz"]:
        response = requests.delete(f"{base_url}{path}", headers=headers, timeout=10)
        assert response.status_code == 400, f"DELETE {path}: {response.status_code}"
    assert_alive(base_url, headers)


def test_delete_unknown_project_returns_404(base_url, headers):
    response = requests.delete(f"{base_url}/projects/{uuid.uuid4()}", headers=headers, timeout=10)
    assert response.status_code == 404
    assert_alive(base_url, headers)


def test_delete_project_as_creator(base_url, headers, project):
    pid = project["@id"]
    response = requests.delete(f"{base_url}/projects/{pid}", headers=headers, timeout=10)
    assert response.status_code == 200
    assert response.json()["@id"] == pid
    assert requests.get(f"{base_url}/projects/{pid}", headers=headers, timeout=10).status_code == 404
    assert_alive(base_url, headers)


def test_delete_unknown_branch_returns_404(base_url, headers, project):
    response = requests.delete(
        f"{base_url}/projects/{project['@id']}/branches/{uuid.uuid4()}", headers=headers, timeout=10
    )
    assert response.status_code == 404
    assert_alive(base_url, headers)


def test_delete_default_branch_returns_409(base_url, headers, project):
    pid = project["@id"]
    default_branch = project["defaultBranch"]["@id"]
    response = requests.delete(f"{base_url}/projects/{pid}/branches/{default_branch}", headers=headers, timeout=10)
    assert response.status_code == 409
    branches = requests.get(f"{base_url}/projects/{pid}/branches", headers=headers, timeout=10)
    assert branches.status_code == 200
    assert default_branch in [b["@id"] for b in branches.json()]


def test_tags_and_missing_entries_do_not_crash(base_url, headers, project):
    pid = project["@id"]
    response = requests.get(f"{base_url}/projects/{pid}/tags", headers=headers, timeout=10)
    assert response.status_code == 200
    response = requests.get(f"{base_url}/projects/{pid}/tags/{uuid.uuid4()}", headers=headers, timeout=10)
    assert response.status_code == 404
    assert_alive(base_url, headers)


@pytest.mark.parametrize(
    "change",
    [
        {"@type": "DataVersion", "payload": {"@type": "Package", "@id": str(uuid.uuid4()), "declaredName": "SmokeTest"}},
        {"@type": "DataVersion", "payload": {"@type": "NoSuchType", "@id": str(uuid.uuid4())}},
        {"@type": "DataVersion", "payload": {"declaredName": "no type"}},
        {"@type": "DataVersion", "payload": None},
        {"@type": "DataVersion"},
        "not an object",
        42,
        None,
    ],
    ids=["unknown_package", "unknown_type", "no_type", "null_payload", "missing_payload", "string_change", "number_change", "null_change"],
)
def test_commit_with_invalid_change_returns_400(base_url, headers, project, change):
    body = {"@type": "CommitRequest", "description": "smoke commit", "change": [change]}
    response = requests.post(f"{base_url}/projects/{project['@id']}/commits", json=body, headers=headers, timeout=10)
    assert response.status_code == 400, f"{response.status_code} {response.text}"
    assert_alive(base_url, headers)


@pytest.mark.parametrize("body", [{"description": "x"}, {"description": "x", "change": {}}, {"change": "x"}], ids=["no_change", "object_change", "string_change"])
def test_commit_with_invalid_change_list_returns_400(base_url, headers, project, body):
    response = requests.post(f"{base_url}/projects/{project['@id']}/commits", json=body, headers=headers, timeout=10)
    assert response.status_code == 400, f"{response.status_code} {response.text}"
    assert_alive(base_url, headers)
