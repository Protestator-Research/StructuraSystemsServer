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
        json={"name": f"branches_{uuid.uuid4().hex[:8]}", "description": "branch tests"},
        headers=headers,
        timeout=10,
    )
    assert response.status_code == 201, response.text
    return response.json()


@pytest.fixture
def commit_id(base_url, headers, project):
    element_id = str(uuid.uuid4())
    body = {
        "@type": "CommitRequest",
        "description": "branch head",
        "change": [
            {
                "@type": "DataVersion",
                "payload": {"@id": element_id, "@type": "Element", "elementId": element_id, "declaredName": "X"},
            }
        ],
    }
    response = requests.post(f"{base_url}/projects/{project['@id']}/commits", json=body, headers=headers, timeout=10)
    assert response.status_code == 201, response.text
    return response.json()["@id"]


def post_branch(base_url, headers, project_id, body):
    return requests.post(f"{base_url}/projects/{project_id}/branches", json=body, headers=headers, timeout=10)


def test_create_branch_returns_branch(base_url, headers, project, commit_id):
    name = f"b_{uuid.uuid4().hex[:6]}"
    response = post_branch(base_url, headers, project["@id"], {"@type": "Branch", "name": name, "head": {"@id": commit_id}})
    assert response.status_code == 201, response.text
    assert response.json()["name"] == name


def test_duplicate_branch_name_returns_409(base_url, headers, project, commit_id):
    body = {"@type": "Branch", "name": "dup", "head": {"@id": commit_id}}
    assert post_branch(base_url, headers, project["@id"], body).status_code == 201
    response = post_branch(base_url, headers, project["@id"], body)
    assert response.status_code == 409, response.text


@pytest.mark.parametrize(
    "body",
    [
        {"@type": "Branch", "name": "", "head": "COMMIT"},
        {"@type": "Branch", "head": "COMMIT"},
        {"@type": "Branch", "name": "nohead"},
        {"@type": "Branch", "name": "badhead", "head": "abc"},
    ],
    ids=["empty_name", "missing_name", "missing_head", "malformed_head"],
)
def test_invalid_branch_request_returns_400(base_url, headers, project, commit_id, body):
    body = dict(body)
    if body.get("head") == "COMMIT":
        body["head"] = {"@id": commit_id}
    response = post_branch(base_url, headers, project["@id"], body)
    assert response.status_code == 400, response.text


def test_unknown_head_returns_404(base_url, headers, project):
    body = {"@type": "Branch", "name": "ghost", "head": {"@id": str(uuid.uuid4())}}
    response = post_branch(base_url, headers, project["@id"], body)
    assert response.status_code == 404, response.text


def test_unknown_project_returns_404(base_url, headers):
    body = {"@type": "Branch", "name": "x", "head": {"@id": str(uuid.uuid4())}}
    response = post_branch(base_url, headers, str(uuid.uuid4()), body)
    assert response.status_code == 404, response.text
