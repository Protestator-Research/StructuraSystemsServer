"""Routes of the project data versioning service: projects, commits, branches, tags, diff and merge."""
import uuid

import pytest
import requests

from conftest import auth


@pytest.fixture
def headers(admin_token):
    return auth(admin_token)


def new_id():
    return str(uuid.uuid4())


def new_project(base_url, headers, name=None):
    response = requests.post(
        f"{base_url}/projects",
        json={"name": name or f"versioning_{uuid.uuid4().hex[:8]}", "description": "versioning api"},
        headers=headers,
        timeout=10,
    )
    assert response.status_code == 201, response.text
    return response.json()


@pytest.fixture
def project(base_url, headers):
    return new_project(base_url, headers)


def package(name, element_id=None):
    return {"@type": "Package", "@id": element_id or str(uuid.uuid4()), "declaredName": name}


def specialization(source, target):
    return {"@type": "Specialization", "@id": str(uuid.uuid4()), "source": [{"@id": source}], "target": [{"@id": target}]}


def post_commit(base_url, headers, project_id, payloads=(), deletions=(), params=None, **extra):
    change = [{"@type": "DataVersion", "payload": payload} for payload in payloads]
    change += [{"@type": "DataVersion", "identity": {"@id": identity}, "payload": None} for identity in deletions]
    body = {"@type": "Commit", "description": "test", "change": change, **extra}
    return requests.post(f"{base_url}/projects/{project_id}/commits", json=body, headers=headers, params=params, timeout=10)


def commit_ok(base_url, headers, project_id, payloads=(), deletions=(), params=None, **extra):
    response = post_commit(base_url, headers, project_id, payloads, deletions, params, **extra)
    assert response.status_code == 201, response.text
    return response.json()


def get_branch(base_url, headers, project_id, branch_id):
    response = requests.get(f"{base_url}/projects/{project_id}/branches/{branch_id}", headers=headers, timeout=10)
    assert response.status_code == 200, response.text
    return response.json()


def new_branch(base_url, headers, project_id, name, head):
    response = requests.post(f"{base_url}/projects/{project_id}/branches",
                             json={"@type": "Branch", "name": name, "head": {"@id": head}}, headers=headers, timeout=10)
    assert response.status_code == 201, response.text
    return response.json()


def get_changes(base_url, headers, project_id, commit_id, query=""):
    return requests.get(f"{base_url}/projects/{project_id}/commits/{commit_id}/changes{query}", headers=headers, timeout=10)


# --- POST /projects, PUT /projects/{id} -----------------------------------------------------------------------------


@pytest.mark.parametrize("body", [{"description": "x"}, {"name": ""}, {"name": "   "}, {"name": 5}, {"name": None}],
                         ids=["missing", "empty", "blank", "number", "null"])
def test_create_project_without_valid_name_returns_400(base_url, headers, body):
    response = requests.post(f"{base_url}/projects", json=body, headers=headers, timeout=10)
    assert response.status_code == 400, response.text
    assert "error" in response.json()


def test_update_project_changes_only_what_is_given(base_url, headers, project):
    pid = project["@id"]
    response = requests.put(f"{base_url}/projects/{pid}", json={"description": "new description"}, headers=headers, timeout=10)
    assert response.status_code == 200, response.text
    assert response.json()["name"] == project["name"]
    assert response.json()["description"] == "new description"

    response = requests.put(f"{base_url}/projects/{pid}", json={"name": "renamed"}, headers=headers, timeout=10)
    assert response.status_code == 200, response.text
    assert response.json()["name"] == "renamed"
    assert response.json()["description"] == "new description"
    assert requests.get(f"{base_url}/projects/{pid}", headers=headers, timeout=10).json()["name"] == "renamed"


def test_update_project_default_branch(base_url, headers, project):
    pid = project["@id"]
    head = commit_ok(base_url, headers, pid, [package("A")])["@id"]
    dev = new_branch(base_url, headers, pid, "dev", head)
    response = requests.put(f"{base_url}/projects/{pid}", json={"defaultBranch": {"@id": dev["@id"]}}, headers=headers, timeout=10)
    assert response.status_code == 200, response.text
    assert response.json()["defaultBranch"] == {"@id": dev["@id"]}
    assert requests.get(f"{base_url}/projects/{pid}", headers=headers, timeout=10).json()["defaultBranch"] == {"@id": dev["@id"]}
    # commits without a branch parameter now go to the new default branch
    commit_ok(base_url, headers, pid, [package("B")])
    assert get_branch(base_url, headers, pid, dev["@id"])["head"] != {"@id": head}


def test_update_project_with_unknown_or_foreign_branch_returns_404(base_url, headers, project):
    pid = project["@id"]
    for branch_id in (str(uuid.uuid4()), new_project(base_url, headers)["defaultBranch"]["@id"]):
        response = requests.put(f"{base_url}/projects/{pid}", json={"name": "ignored", "defaultBranch": {"@id": branch_id}},
                                headers=headers, timeout=10)
        assert response.status_code == 404, response.text
    current = requests.get(f"{base_url}/projects/{pid}", headers=headers, timeout=10).json()
    assert current["defaultBranch"] == project["defaultBranch"]
    assert current["name"] == project["name"]


@pytest.mark.parametrize("body", [{"name": ""}, {"name": 3}, {"description": 3}, {"defaultBranch": "main"}, {"defaultBranch": {"@id": "x"}}],
                         ids=["empty_name", "number_name", "number_description", "string_branch", "bad_branch_id"])
def test_update_project_with_invalid_body_returns_400(base_url, headers, project, body):
    response = requests.put(f"{base_url}/projects/{project['@id']}", json=body, headers=headers, timeout=10)
    assert response.status_code == 400, response.text


def test_update_project_unknown_and_foreign(base_url, headers, project, make_login):
    assert requests.put(f"{base_url}/projects/{uuid.uuid4()}", json={"name": "x"}, headers=headers, timeout=10).status_code == 404
    assert requests.put(f"{base_url}/projects/not-a-uuid", json={"name": "x"}, headers=headers, timeout=10).status_code == 400
    assert requests.put(f"{base_url}/projects/{project['@id']}", data="{bad", headers={**headers, "Content-Type": "application/json"},
                        timeout=10).status_code == 400
    _, token = make_login()
    stranger = requests.put(f"{base_url}/projects/{project['@id']}", json={"name": "hijacked"}, headers=auth(token), timeout=10)
    assert stranger.status_code == 404


# --- POST commits -----------------------------------------------------------------------------------------------------


def test_first_commit_has_no_previous_commit_and_moves_the_default_branch(base_url, headers, project):
    pid = project["@id"]
    assert requests.get(f"{base_url}/projects/{pid}/commits", headers=headers, timeout=10).json() == []
    branch = get_branch(base_url, headers, pid, project["defaultBranch"]["@id"])
    assert branch["head"] is None
    assert branch["owningProject"] == {"@id": pid}
    assert branch["name"] == "main"

    commit = commit_ok(base_url, headers, pid, [package("A")])
    assert commit["previousCommits"] == []
    assert commit["owningProject"] == {"@id": pid}
    assert get_branch(base_url, headers, pid, branch["@id"])["head"] == {"@id": commit["@id"]}
    second = commit_ok(base_url, headers, pid, [package("B")])
    assert second["previousCommits"] == [{"@id": commit["@id"]}]
    assert second["created"] > commit["created"]
    # every DataVersion has its own id and refers to its commit
    version = second["change"][0]
    assert version["@id"] != version["identity"]["@id"]
    assert version["commit"] == {"@id": second["@id"]}


def test_commit_on_a_branch_by_id(base_url, headers, project):
    pid = project["@id"]
    first = commit_ok(base_url, headers, pid, [package("A")])["@id"]
    dev = new_branch(base_url, headers, pid, "dev", first)
    main_id = project["defaultBranch"]["@id"]

    commit = commit_ok(base_url, headers, pid, [package("B")], params={"branchId": dev["@id"]})
    assert get_branch(base_url, headers, pid, dev["@id"])["head"] == {"@id": commit["@id"]}
    assert get_branch(base_url, headers, pid, main_id)["head"] == {"@id": first}
    assert commit["previousCommits"] == [{"@id": first}]

    # the alias by name
    by_name = commit_ok(base_url, headers, pid, [package("C")], params={"branch": "dev"})
    assert get_branch(base_url, headers, pid, dev["@id"])["head"] == {"@id": by_name["@id"]}
    assert get_branch(base_url, headers, pid, main_id)["head"] == {"@id": first}


def test_commit_on_unknown_or_invalid_branch(base_url, headers, project):
    pid = project["@id"]
    main_id = project["defaultBranch"]["@id"]
    assert post_commit(base_url, headers, pid, [package("A")], params={"branchId": "xyz"}).status_code == 400
    assert post_commit(base_url, headers, pid, [package("A")], params={"branchId": str(uuid.uuid4())}).status_code == 404
    assert post_commit(base_url, headers, pid, [package("A")], params={"branch": "nope"}).status_code == 404
    # a branch of another project is unknown here
    foreign = new_project(base_url, headers)["defaultBranch"]["@id"]
    assert post_commit(base_url, headers, pid, [package("A")], params={"branchId": foreign}).status_code == 404
    # nothing happened
    assert get_branch(base_url, headers, pid, main_id)["head"] is None
    assert requests.get(f"{base_url}/projects/{pid}/commits", headers=headers, timeout=10).json() == []


def test_commit_previous_commits_are_head_and_requested(base_url, headers, project):
    pid = project["@id"]
    first = commit_ok(base_url, headers, pid, [package("A")])["@id"]
    dev = new_branch(base_url, headers, pid, "dev", first)
    on_dev = commit_ok(base_url, headers, pid, [package("B")], params={"branchId": dev["@id"]})["@id"]
    head = commit_ok(base_url, headers, pid, [package("C")])["@id"]

    merged = commit_ok(base_url, headers, pid, [package("D")], previousCommits=[{"@id": on_dev}, {"@id": head}])
    assert {p["@id"] for p in merged["previousCommits"]} == {head, on_dev}
    assert len(merged["previousCommits"]) == 2


def test_commit_with_unknown_or_foreign_previous_commit_returns_404(base_url, headers, project):
    pid = project["@id"]
    commit_ok(base_url, headers, pid, [package("A")])
    other = new_project(base_url, headers)
    foreign = commit_ok(base_url, headers, other["@id"], [package("F")])["@id"]
    for previous in (str(uuid.uuid4()), foreign):
        response = post_commit(base_url, headers, pid, [package("B")], previousCommits=[{"@id": previous}])
        assert response.status_code == 404, response.text
    assert len(requests.get(f"{base_url}/projects/{pid}/commits", headers=headers, timeout=10).json()) == 1


def test_commit_deletion_with_cascade(base_url, headers, project):
    pid = project["@id"]
    a, b = package("A"), package("B")
    link = specialization(a["@id"], b["@id"])
    commit_ok(base_url, headers, pid, [a, b, link])

    deleted = commit_ok(base_url, headers, pid, deletions=[a["@id"]])
    identities = sorted(version["identity"]["@id"] for version in deleted["change"])
    assert identities == sorted([a["@id"], link["@id"]])
    assert all(version["payload"] is None for version in deleted["change"])

    changes = get_changes(base_url, headers, pid, deleted["@id"], "?changeTypes=DELETED")
    assert changes.status_code == 200
    assert len(changes.json()) == 2
    assert get_changes(base_url, headers, pid, deleted["@id"], "?changeTypes=CREATED,UPDATED").json() == []

    # a payload that is null is a deletion, a change without payload key as well
    c = package("C")
    commit_ok(base_url, headers, pid, [c])
    body = {"change": [{"identity": {"@id": c["@id"]}}]}
    assert requests.post(f"{base_url}/projects/{pid}/commits", json=body, headers=headers, timeout=10).status_code == 201


def test_commit_deletion_of_unknown_identity_returns_400(base_url, headers, project):
    pid = project["@id"]
    commit_ok(base_url, headers, pid, [package("A")])
    response = post_commit(base_url, headers, pid, deletions=[str(uuid.uuid4())])
    assert response.status_code == 400, response.text
    assert "error" in response.json()


def test_commit_with_duplicate_identity_returns_400(base_url, headers, project):
    element_id = str(uuid.uuid4())
    response = post_commit(base_url, headers, project["@id"], [package("A", element_id), package("B", element_id)])
    assert response.status_code == 400, response.text


def test_commit_unknown_payload_type_returns_400(base_url, headers, project):
    response = post_commit(base_url, headers, project["@id"], [{"@type": "NoSuchType", "@id": str(uuid.uuid4())}])
    assert response.status_code == 400, response.text


# --- GET changes ----------------------------------------------------------------------------------------------------


def test_changes_filter_by_change_types(base_url, headers, project):
    pid = project["@id"]
    a, b = package("A"), package("B")
    commit_ok(base_url, headers, pid, [a, b])
    mixed = commit_ok(base_url, headers, pid, [package("A renamed", a["@id"]), package("C")], deletions=[b["@id"]])
    cid = mixed["@id"]

    def types(query):
        response = get_changes(base_url, headers, pid, cid, query)
        assert response.status_code == 200, response.text
        return len(response.json())

    assert types("") == 3
    assert types("?changeTypes=CREATED") == 1
    assert types("?changeTypes=UPDATED") == 1
    assert types("?changeTypes=DELETED") == 1
    assert types("?changeTypes=CREATED,UPDATED") == 2
    assert types("?changeTypes=CREATED&changeTypes=DELETED") == 2
    assert types("?change_type=DELETED") == 1
    assert types("?changeTypes=") == 3

    for query in ("?changeTypes=NOPE", "?changeTypes=CREATED,NOPE", "?change_type=created"):
        assert get_changes(base_url, headers, pid, cid, query).status_code == 400

    # a change is found by the id of its DataVersion
    version_id = mixed["change"][0]["@id"]
    found = requests.get(f"{base_url}/projects/{pid}/commits/{cid}/changes/{version_id}", headers=headers, timeout=10)
    assert found.status_code == 200
    assert found.json()["@id"] == version_id


# --- error bodies ---------------------------------------------------------------------------------------------------


def test_not_found_answers_have_a_json_body(base_url, headers, project):
    pid = project["@id"]
    for path in (f"/projects/{pid}/commits/{uuid.uuid4()}", f"/projects/{pid}/branches/{uuid.uuid4()}",
                 f"/projects/{pid}/tags/{uuid.uuid4()}", f"/projects/{pid}/commits/{uuid.uuid4()}/changes",
                 f"/projects/{pid}/commits/{uuid.uuid4()}/elements"):
        response = requests.get(f"{base_url}{path}", headers=headers, timeout=10)
        assert response.status_code == 404, path
        assert response.headers["Content-Type"].startswith("application/json")
        assert "error" in response.json(), path


# --- branches -------------------------------------------------------------------------------------------------------


def test_create_branch_returns_201_with_owning_project(base_url, headers, project):
    pid = project["@id"]
    head = commit_ok(base_url, headers, pid, [package("A")])["@id"]
    branch = new_branch(base_url, headers, pid, "feature", head)
    assert branch["head"] == {"@id": head}
    assert branch["owningProject"] == {"@id": pid}


# --- tags -----------------------------------------------------------------------------------------------------------


def test_tag_lifecycle(base_url, headers, project):
    pid = project["@id"]
    commit_id = commit_ok(base_url, headers, pid, [package("A")])["@id"]

    created = requests.post(f"{base_url}/projects/{pid}/tags", json={"name": "v1", "taggedCommit": {"@id": commit_id}},
                            headers=headers, timeout=10)
    assert created.status_code == 201, created.text
    tag = created.json()
    assert tag["name"] == "v1"
    assert tag["taggedCommit"] == {"@id": commit_id}
    assert tag["owningProject"] == {"@id": pid}

    duplicate = requests.post(f"{base_url}/projects/{pid}/tags", json={"name": "v1", "taggedCommit": {"@id": commit_id}},
                              headers=headers, timeout=10)
    assert duplicate.status_code == 409

    assert [t["@id"] for t in requests.get(f"{base_url}/projects/{pid}/tags", headers=headers, timeout=10).json()] == [tag["@id"]]
    assert requests.get(f"{base_url}/projects/{pid}/tags/{tag['@id']}", headers=headers, timeout=10).status_code == 200

    deleted = requests.delete(f"{base_url}/projects/{pid}/tags/{tag['@id']}", headers=headers, timeout=10)
    assert deleted.status_code == 200, deleted.text
    assert deleted.json()["@id"] == tag["@id"]
    assert requests.get(f"{base_url}/projects/{pid}/tags/{tag['@id']}", headers=headers, timeout=10).status_code == 404
    assert requests.delete(f"{base_url}/projects/{pid}/tags/{tag['@id']}", headers=headers, timeout=10).status_code == 404
    assert requests.delete(f"{base_url}/projects/{pid}/tags/xyz", headers=headers, timeout=10).status_code == 400

    # the name can be used again after the deletion
    again = requests.post(f"{base_url}/projects/{pid}/tags", json={"name": "v1", "taggedCommit": {"@id": commit_id}},
                          headers=headers, timeout=10)
    assert again.status_code == 201


def test_tag_with_invalid_request(base_url, headers, project):
    pid = project["@id"]
    commit_id = commit_ok(base_url, headers, pid, [package("A")])["@id"]
    other = new_project(base_url, headers)
    foreign = commit_ok(base_url, headers, other["@id"], [package("F")])["@id"]
    cases = [
        ({"taggedCommit": {"@id": commit_id}}, 400),
        ({"name": "", "taggedCommit": {"@id": commit_id}}, 400),
        ({"name": "t"}, 400),
        ({"name": "t", "taggedCommit": "abc"}, 400),
        ({"name": "t", "taggedCommit": {"@id": "xyz"}}, 400),
        ({"name": "t", "taggedCommit": {"@id": str(uuid.uuid4())}}, 404),
        ({"name": "t", "taggedCommit": {"@id": foreign}}, 404),
    ]
    for body, status in cases:
        response = requests.post(f"{base_url}/projects/{pid}/tags", json=body, headers=headers, timeout=10)
        assert response.status_code == status, f"{body}: {response.status_code} {response.text}"
    assert requests.get(f"{base_url}/projects/{pid}/tags", headers=headers, timeout=10).json() == []


# --- diff -----------------------------------------------------------------------------------------------------------


def test_diff(base_url, headers, project):
    pid = project["@id"]
    kept, changed, removed = package("Kept"), package("Changed"), package("Removed")
    base_commit = commit_ok(base_url, headers, pid, [kept, changed, removed])["@id"]
    added = package("Added")
    compare = commit_ok(base_url, headers, pid, [package("Changed 2", changed["@id"]), added], deletions=[removed["@id"]])["@id"]

    def diff(query):
        response = requests.get(f"{base_url}/projects/{pid}/commits/{compare}/diff?baseCommitId={base_commit}{query}",
                                headers=headers, timeout=10)
        assert response.status_code == 200, response.text
        return response.json()

    differences = diff("")
    assert len(differences) == 3
    by_kind = {}
    for entry in differences:
        assert entry["@type"] == "DataDifference"
        kind = ("base" if entry["baseData"] else "") + ("compare" if entry["compareData"] else "")
        by_kind[kind] = entry
    assert set(by_kind) == {"base", "compare", "basecompare"}
    assert by_kind["compare"]["compareData"]["payload"]["@id"] == added["@id"]
    assert by_kind["base"]["baseData"]["payload"]["@id"] == removed["@id"]
    assert by_kind["basecompare"]["compareData"]["payload"]["declaredName"] == "Changed 2"

    assert len(diff("&changeTypes=CREATED")) == 1
    assert len(diff("&changeTypes=UPDATED")) == 1
    assert len(diff("&changeTypes=DELETED")) == 1
    assert len(diff("&changeTypes=CREATED,DELETED")) == 2
    # reversed: what was created is now deleted
    reverse = requests.get(f"{base_url}/projects/{pid}/commits/{base_commit}/diff?baseCommitId={compare}&changeTypes=DELETED",
                           headers=headers, timeout=10).json()
    assert [entry["baseData"]["payload"]["@id"] for entry in reverse] == [added["@id"]]


def test_diff_with_invalid_parameters(base_url, headers, project):
    pid = project["@id"]
    commit_id = commit_ok(base_url, headers, pid, [package("A")])["@id"]
    other = new_project(base_url, headers)
    foreign = commit_ok(base_url, headers, other["@id"], [package("F")])["@id"]
    url = f"{base_url}/projects/{pid}/commits/{commit_id}/diff"
    assert requests.get(url, headers=headers, timeout=10).status_code == 400
    assert requests.get(url, params={"baseCommitId": "xyz"}, headers=headers, timeout=10).status_code == 400
    assert requests.get(url, params={"baseCommitId": commit_id, "changeTypes": "NOPE"}, headers=headers, timeout=10).status_code == 400
    assert requests.get(url, params={"baseCommitId": str(uuid.uuid4())}, headers=headers, timeout=10).status_code == 404
    assert requests.get(url, params={"baseCommitId": foreign}, headers=headers, timeout=10).status_code == 404
    assert requests.get(f"{base_url}/projects/{pid}/commits/{uuid.uuid4()}/diff", params={"baseCommitId": commit_id},
                        headers=headers, timeout=10).status_code == 404
    assert requests.get(f"{base_url}/projects/{pid}/commits/xyz/diff", params={"baseCommitId": commit_id},
                        headers=headers, timeout=10).status_code == 400


# --- merge ----------------------------------------------------------------------------------------------------------


def two_branches(base_url, headers, project, elements):
    """main and dev start at the same commit with the given elements."""
    pid = project["@id"]
    root = commit_ok(base_url, headers, pid, elements)["@id"]
    dev = new_branch(base_url, headers, pid, "dev", root)
    return pid, project["defaultBranch"]["@id"], dev["@id"], root


def test_merge_without_conflict(base_url, headers, project):
    p1, p2 = package("P1"), package("P2")
    pid, main_id, dev_id, _ = two_branches(base_url, headers, project, [p1, p2])
    main_head = commit_ok(base_url, headers, pid, [package("P1 main", p1["@id"])])["@id"]
    dev_head = commit_ok(base_url, headers, pid, [package("P2 dev", p2["@id"])], params={"branchId": dev_id})["@id"]

    response = requests.post(f"{base_url}/projects/{pid}/branches/{main_id}/merge",
                             params={"sourceCommitId": dev_head, "description": "merge dev"}, headers=headers, timeout=10)
    assert response.status_code == 200, response.text
    merge = response.json()
    assert merge["@type"] == "Commit"
    assert merge["description"] == "merge dev"
    assert [p["@id"] for p in merge["previousCommits"]] == [main_head, dev_head]
    assert get_branch(base_url, headers, pid, main_id)["head"] == {"@id": merge["@id"]}
    assert get_branch(base_url, headers, pid, dev_id)["head"] == {"@id": dev_head}

    # against the merge both changes are visible
    diff = requests.get(f"{base_url}/projects/{pid}/commits/{merge['@id']}/diff", params={"baseCommitId": dev_head}, headers=headers, timeout=10).json()
    assert [entry["compareData"]["payload"]["declaredName"] for entry in diff] == ["P1 main"]


def test_merge_conflict_and_resolution(base_url, headers, project):
    p1 = package("P1")
    pid, main_id, dev_id, _ = two_branches(base_url, headers, project, [p1])
    main_head = commit_ok(base_url, headers, pid, [package("P1 main", p1["@id"])])["@id"]
    dev_head = commit_ok(base_url, headers, pid, [package("P1 dev", p1["@id"])], params={"branchId": dev_id})["@id"]
    url = f"{base_url}/projects/{pid}/branches/{main_id}/merge"

    conflict = requests.post(url, params={"sourceCommitId": dev_head}, headers=headers, timeout=10)
    assert conflict.status_code == 409, conflict.text
    assert [entry["@id"] for entry in conflict.json()] == [p1["@id"]]
    assert get_branch(base_url, headers, pid, main_id)["head"] == {"@id": main_head}
    assert len(requests.get(f"{base_url}/projects/{pid}/commits", headers=headers, timeout=10).json()) == 3

    # a resolution for another element does not help
    wrong = requests.post(url, params={"sourceCommitId": dev_head}, json=[package("other")], headers=headers, timeout=10)
    assert wrong.status_code == 409

    resolved = requests.post(url, params={"sourceCommitId": dev_head}, json=[package("P1 resolved", p1["@id"])], headers=headers, timeout=10)
    assert resolved.status_code == 200, resolved.text
    merge = resolved.json()
    assert get_branch(base_url, headers, pid, main_id)["head"] == {"@id": merge["@id"]}
    payloads = [version["payload"]["declaredName"] for version in merge["change"] if version["payload"]]
    assert payloads == ["P1 resolved"]


def test_merge_with_several_source_commits(base_url, headers, project):
    p1 = package("P1")
    pid, main_id, dev_id, root = two_branches(base_url, headers, project, [p1])
    feature = new_branch(base_url, headers, pid, "feature", root)["@id"]
    on_dev = commit_ok(base_url, headers, pid, [package("D")], params={"branchId": dev_id})["@id"]
    on_feature = commit_ok(base_url, headers, pid, [package("F")], params={"branchId": feature})["@id"]

    response = requests.post(f"{base_url}/projects/{pid}/branches/{main_id}/merge",
                             params=[("sourceCommitId", on_dev), ("sourceCommitId", on_feature)], headers=headers, timeout=10)
    assert response.status_code == 200, response.text
    # the head of main (root) is an ancestor of both commits, so it adds nothing as a parent and is dropped
    assert {p["@id"] for p in response.json()["previousCommits"]} == {on_dev, on_feature}


def test_merge_with_invalid_request(base_url, headers, project):
    p1 = package("P1")
    pid, main_id, dev_id, root = two_branches(base_url, headers, project, [p1])
    other = new_project(base_url, headers)
    foreign = commit_ok(base_url, headers, other["@id"], [package("F")])["@id"]
    url = f"{base_url}/projects/{pid}/branches/{main_id}/merge"

    assert requests.post(url, headers=headers, timeout=10).status_code == 400
    assert requests.post(url, params={"sourceCommitId": "xyz"}, headers=headers, timeout=10).status_code == 400
    assert requests.post(url, params={"sourceCommitId": str(uuid.uuid4())}, headers=headers, timeout=10).status_code == 404
    assert requests.post(url, params={"sourceCommitId": foreign}, headers=headers, timeout=10).status_code == 404
    assert requests.post(url, params={"sourceCommitId": root}, data="{bad", headers={**headers, "Content-Type": "application/json"},
                         timeout=10).status_code == 400
    assert requests.post(url, params={"sourceCommitId": root}, json={"not": "an array"}, headers=headers, timeout=10).status_code == 400
    assert requests.post(url, params={"sourceCommitId": root}, json=[{"@type": "NoSuchType", "@id": str(uuid.uuid4())}],
                         headers=headers, timeout=10).status_code == 400
    assert requests.post(f"{base_url}/projects/{pid}/branches/{uuid.uuid4()}/merge", params={"sourceCommitId": root},
                         headers=headers, timeout=10).status_code == 404
    assert requests.post(f"{base_url}/projects/{pid}/branches/xyz/merge", params={"sourceCommitId": root},
                         headers=headers, timeout=10).status_code == 400
    # nothing was merged
    assert get_branch(base_url, headers, pid, main_id)["head"] == {"@id": root}


def test_versioning_routes_require_write_access(base_url, headers, project, make_login):
    pid = project["@id"]
    commit_id = commit_ok(base_url, headers, pid, [package("A")])["@id"]
    _, token = make_login()
    stranger = auth(token)
    calls = [
        requests.post(f"{base_url}/projects/{pid}/tags", json={"name": "x", "taggedCommit": {"@id": commit_id}}, headers=stranger, timeout=10),
        requests.delete(f"{base_url}/projects/{pid}/tags/{uuid.uuid4()}", headers=stranger, timeout=10),
        requests.get(f"{base_url}/projects/{pid}/commits/{commit_id}/diff", params={"baseCommitId": commit_id}, headers=stranger, timeout=10),
        requests.post(f"{base_url}/projects/{pid}/branches/{project['defaultBranch']['@id']}/merge", params={"sourceCommitId": commit_id},
                      headers=stranger, timeout=10),
    ]
    assert [call.status_code for call in calls] == [404, 404, 404, 404]


# --- previous commits of a client commit ---------------------------------------------------------------------------------


def test_client_commit_with_an_ancestor_as_previous_commit_does_not_bring_back_deleted_data(base_url, headers, project):
    pid = project["@id"]
    p1, p2 = package("P1"), package("P2")
    a = commit_ok(base_url, headers, pid, [p1, p2])["@id"]
    commit_ok(base_url, headers, pid, [package("P1 changed", p1["@id"])])
    c = commit_ok(base_url, headers, pid, deletions=[p2["@id"]])["@id"]

    # A is an ancestor of the head C: it is dropped from the parents and does not bring P2 and the old P1 back
    d = commit_ok(base_url, headers, pid, [package("P3")], previousCommits=[{"@id": a}])
    assert d["previousCommits"] == [{"@id": c}]
    missing = requests.get(f"{base_url}/projects/{pid}/commits/{d['@id']}/elements/{p2['@id']}", headers=headers, timeout=10)
    assert missing.status_code == 404
    kept = requests.get(f"{base_url}/projects/{pid}/commits/{d['@id']}/elements/{p1['@id']}", headers=headers, timeout=10)
    assert kept.json()["declaredName"] == "P1 changed"
    listed = requests.get(f"{base_url}/projects/{pid}/commits/{d['@id']}/elements", headers=headers, timeout=10).json()
    assert len(listed) == 2


def test_client_commit_with_diverging_previous_commits_takes_over_the_merge(base_url, headers, project):
    pid = project["@id"]
    p1, p2 = package("P1"), package("P2")
    root = commit_ok(base_url, headers, pid, [p1, p2])["@id"]
    dev = new_branch(base_url, headers, pid, "dev", root)["@id"]
    on_dev = commit_ok(base_url, headers, pid, [package("P2 dev", p2["@id"])], params={"branchId": dev})["@id"]
    on_main = commit_ok(base_url, headers, pid, deletions=[p1["@id"]])["@id"]

    # P1 was deleted on main, P2 changed on dev: the commit with both parents has the result of both
    merged = commit_ok(base_url, headers, pid, [package("P3")], previousCommits=[{"@id": on_dev}])
    assert sorted(p["@id"] for p in merged["previousCommits"]) == sorted([on_main, on_dev])
    elements = {e["@id"]: e["declaredName"] for e in requests.get(
        f"{base_url}/projects/{pid}/commits/{merged['@id']}/elements", headers=headers, timeout=10).json()}
    assert p1["@id"] not in elements
    assert elements[p2["@id"]] == "P2 dev"
    assert len(elements) == 2


def test_client_commit_with_conflicting_previous_commits_needs_a_resolution(base_url, headers, project):
    pid = project["@id"]
    p1 = package("P1")
    root = commit_ok(base_url, headers, pid, [p1])["@id"]
    dev = new_branch(base_url, headers, pid, "dev", root)["@id"]
    on_dev = commit_ok(base_url, headers, pid, [package("P1 dev", p1["@id"])], params={"branchId": dev})["@id"]
    commit_ok(base_url, headers, pid, [package("P1 main", p1["@id"])])
    before = len(requests.get(f"{base_url}/projects/{pid}/commits", headers=headers, timeout=10).json())

    rejected = post_commit(base_url, headers, pid, [package("P2")], previousCommits=[{"@id": on_dev}])
    assert rejected.status_code == 400, rejected.text
    assert p1["@id"] in rejected.json()["error"]
    assert len(requests.get(f"{base_url}/projects/{pid}/commits", headers=headers, timeout=10).json()) == before

    resolved = commit_ok(base_url, headers, pid, [package("P1 resolved", p1["@id"])], previousCommits=[{"@id": on_dev}])
    element = requests.get(f"{base_url}/projects/{pid}/commits/{resolved['@id']}/elements/{p1['@id']}", headers=headers, timeout=10).json()
    assert element["declaredName"] == "P1 resolved"


# --- deleting a subtree ---------------------------------------------------------------------------------------------------


def test_deleting_a_package_deletes_its_subtree(base_url, headers, project):
    pid = project["@id"]
    root, child, other = package("Root"), package("Child"), package("Other")
    membership = {"@type": "OwningMembership", "@id": new_id(), "source": [{"@id": root["@id"]}], "target": [{"@id": child["@id"]}],
                  "owningRelatedElement": {"@id": root["@id"]}, "ownedRelatedElement": [{"@id": child["@id"]}]}
    on_child = specialization(other["@id"], child["@id"])
    commit_ok(base_url, headers, pid, [root, child, other, membership, on_child])

    deleted = commit_ok(base_url, headers, pid, deletions=[root["@id"]])
    assert sorted(v["identity"]["@id"] for v in deleted["change"]) == sorted([root["@id"], membership["@id"], child["@id"], on_child["@id"]])
    elements = requests.get(f"{base_url}/projects/{pid}/commits/{deleted['@id']}/elements", headers=headers, timeout=10).json()
    assert [e["@id"] for e in elements] == [other["@id"]]
    roots = requests.get(f"{base_url}/projects/{pid}/commits/{deleted['@id']}/roots", headers=headers, timeout=10).json()
    assert [e["@id"] for e in roots] == [other["@id"]]


# --- the default branch while other requests run ------------------------------------------------------------------------


def test_changing_the_default_branch_in_parallel_with_other_requests(base_url, headers, project):
    from concurrent.futures import ThreadPoolExecutor

    pid = project["@id"]
    first = commit_ok(base_url, headers, pid, [package("A")])["@id"]
    other = new_branch(base_url, headers, pid, "other", first)["@id"]
    main = project["defaultBranch"]["@id"]

    def job(index):
        kind = index % 4
        if kind == 0:
            return requests.put(f"{base_url}/projects/{pid}", json={"defaultBranch": {"@id": other if index % 8 == 0 else main}},
                                headers=headers, timeout=30)
        if kind == 1:
            return requests.get(f"{base_url}/projects/{pid}", headers=headers, timeout=30)
        if kind == 2:
            return post_commit(base_url, headers, pid, [package(f"E{index}")])
        return requests.get(f"{base_url}/projects/{pid}/branches", headers=headers, timeout=30)

    with ThreadPoolExecutor(16) as pool:
        results = list(pool.map(job, range(160)))
    assert all(r.status_code < 500 for r in results), [(r.status_code, r.text[:80]) for r in results if r.status_code >= 500]
    final = requests.get(f"{base_url}/projects/{pid}", headers=headers, timeout=10).json()["defaultBranch"]["@id"]
    assert final in (main, other)
    # the commits went to a branch of the project and all have a single previous commit chain per branch
    branches = requests.get(f"{base_url}/projects/{pid}/branches", headers=headers, timeout=10).json()
    assert {b["@id"] for b in branches} == {main, other}


def test_unknown_branch_in_put_leaves_name_and_default_branch_alone(base_url, headers, project):
    pid = project["@id"]
    response = requests.put(f"{base_url}/projects/{pid}", json={"name": "never", "defaultBranch": {"@id": new_id()}}, headers=headers, timeout=10)
    assert response.status_code == 404
    current = requests.get(f"{base_url}/projects/{pid}", headers=headers, timeout=10).json()
    assert current["name"] == project["name"]
    assert current["defaultBranch"] == project["defaultBranch"]
