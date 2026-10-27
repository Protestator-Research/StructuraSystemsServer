"""Element navigation: elements, relationships and roots are read from the versionedData of a commit."""
import uuid

import pytest
import requests

from conftest import auth


@pytest.fixture
def headers(admin_token):
    return auth(admin_token)


@pytest.fixture
def project(base_url, headers):
    response = requests.post(f"{base_url}/projects",
                             json={"name": f"navigation_{uuid.uuid4().hex[:8]}", "description": "element navigation"},
                             headers=headers, timeout=10)
    assert response.status_code == 201, response.text
    return response.json()


def new_id():
    return str(uuid.uuid4())


def package(name, element_id=None, **extra):
    return {"@type": "Package", "@id": element_id or new_id(), "declaredName": name, **extra}


def specialization(source, target):
    return {"@type": "Specialization", "@id": new_id(), "source": [{"@id": source}], "target": [{"@id": target}]}


def commit(base_url, headers, project_id, payloads=(), deletions=()):
    change = [{"payload": payload} for payload in payloads]
    change += [{"identity": {"@id": identity}, "payload": None} for identity in deletions]
    response = requests.post(f"{base_url}/projects/{project_id}/commits", json={"description": "t", "change": change},
                             headers=headers, timeout=10)
    assert response.status_code == 201, response.text
    return response.json()["@id"]


def get(base_url, headers, path, **params):
    return requests.get(f"{base_url}{path}", headers=headers, params=params, timeout=10)


def ids_of(response):
    assert response.status_code == 200, response.text
    return {item["@id"] for item in response.json()}


# --- elements -------------------------------------------------------------------------------------------------------


def test_elements_are_the_cumulative_state_of_the_commit(base_url, headers, project):
    pid = project["@id"]
    p1, p2 = package("P1"), package("P2")
    first = commit(base_url, headers, pid, [p1, p2])
    second = commit(base_url, headers, pid, [package("P1 renamed", p1["@id"])])

    at_second = get(base_url, headers, f"/projects/{pid}/commits/{second}/elements")
    assert ids_of(at_second) == {p1["@id"], p2["@id"]}
    names = {element["@id"]: element["declaredName"] for element in at_second.json()}
    assert names[p1["@id"]] == "P1 renamed"
    assert names[p2["@id"]] == "P2"

    # an element that an earlier commit created and no later commit touched is found by id
    found = get(base_url, headers, f"/projects/{pid}/commits/{second}/elements/{p2['@id']}")
    assert found.status_code == 200, found.text
    assert found.json()["declaredName"] == "P2"

    # the earlier commit shows the earlier state
    at_first = get(base_url, headers, f"/projects/{pid}/commits/{first}/elements/{p1['@id']}").json()
    assert at_first["declaredName"] == "P1"


def test_deleted_elements_are_not_visible_any_more(base_url, headers, project):
    pid = project["@id"]
    p1, p2 = package("P1"), package("P2")
    first = commit(base_url, headers, pid, [p1, p2])
    second = commit(base_url, headers, pid, deletions=[p2["@id"]])
    third = commit(base_url, headers, pid, [package("P3")])

    for later in (second, third):
        assert p2["@id"] not in ids_of(get(base_url, headers, f"/projects/{pid}/commits/{later}/elements"))
        missing = get(base_url, headers, f"/projects/{pid}/commits/{later}/elements/{p2['@id']}")
        assert missing.status_code == 404
        assert "error" in missing.json()
    assert p2["@id"] in ids_of(get(base_url, headers, f"/projects/{pid}/commits/{first}/elements"))
    assert get(base_url, headers, f"/projects/{pid}/commits/{first}/elements/{p2['@id']}").status_code == 200


def test_elements_of_an_unknown_commit(base_url, headers, project):
    pid = project["@id"]
    assert get(base_url, headers, f"/projects/{pid}/commits/{uuid.uuid4()}/elements").status_code == 404
    assert get(base_url, headers, f"/projects/{pid}/commits/xyz/elements").status_code == 400
    commit_id = commit(base_url, headers, pid, [package("P")])
    assert get(base_url, headers, f"/projects/{pid}/commits/{commit_id}/elements/{uuid.uuid4()}").status_code == 404
    assert get(base_url, headers, f"/projects/{pid}/commits/{commit_id}/elements/xyz").status_code == 400


# --- relationships --------------------------------------------------------------------------------------------------


def test_relationships_by_direction(base_url, headers, project):
    pid = project["@id"]
    p1, p2, p3 = package("P1"), package("P2"), package("P3")
    s12, s23 = specialization(p1["@id"], p2["@id"]), specialization(p2["@id"], p3["@id"])
    # the relationships are created in a later commit than the elements
    commit(base_url, headers, pid, [p1, p2, p3])
    head = commit(base_url, headers, pid, [s12, s23])

    def relationships(element, **params):
        return ids_of(get(base_url, headers, f"/projects/{pid}/commits/{head}/elements/{element['@id']}/relationships", **params))

    assert relationships(p2) == {s12["@id"], s23["@id"]}
    assert relationships(p2, direction="both") == {s12["@id"], s23["@id"]}
    assert relationships(p2, direction="out") == {s23["@id"]}
    assert relationships(p2, direction="in") == {s12["@id"]}
    assert relationships(p1, direction="out") == {s12["@id"]}
    assert relationships(p1, direction="in") == set()
    assert relationships(p3, direction="in") == {s23["@id"]}

    body = get(base_url, headers, f"/projects/{pid}/commits/{head}/elements/{p2['@id']}/relationships", direction="out").json()
    assert body[0]["@type"] == "Specialization"
    assert body[0]["source"] == [{"@id": p2["@id"]}]


def test_relationships_of_a_deleted_element_disappear_with_it(base_url, headers, project):
    pid = project["@id"]
    p1, p2 = package("P1"), package("P2")
    link = specialization(p1["@id"], p2["@id"])
    commit(base_url, headers, pid, [p1, p2, link])
    after = commit(base_url, headers, pid, deletions=[p1["@id"]])
    assert ids_of(get(base_url, headers, f"/projects/{pid}/commits/{after}/elements/{p2['@id']}/relationships")) == set()
    assert get(base_url, headers, f"/projects/{pid}/commits/{after}/elements/{p1['@id']}/relationships").status_code == 404
    assert link["@id"] not in ids_of(get(base_url, headers, f"/projects/{pid}/commits/{after}/elements"))


def test_relationships_with_invalid_requests(base_url, headers, project):
    pid = project["@id"]
    p1 = package("P1")
    commit_id = commit(base_url, headers, pid, [p1])
    base = f"/projects/{pid}/commits/{commit_id}/elements"
    assert get(base_url, headers, f"{base}/{p1['@id']}/relationships", direction="sideways").status_code == 400
    assert get(base_url, headers, f"{base}/{p1['@id']}/relationships", direction="IN").status_code == 400
    assert get(base_url, headers, f"{base}/{uuid.uuid4()}/relationships").status_code == 404
    assert get(base_url, headers, f"{base}/xyz/relationships").status_code == 400
    assert get(base_url, headers, f"/projects/{pid}/commits/{uuid.uuid4()}/elements/{p1['@id']}/relationships").status_code == 404
    assert get(base_url, headers, f"/projects/{pid}/commits/xyz/elements/{p1['@id']}/relationships").status_code == 400
    assert get(base_url, headers, f"{base}/{p1['@id']}/relationships").json() == []


# --- roots ----------------------------------------------------------------------------------------------------------


def owned_model(name):
    """A package with a part definition as member: Package -> OwningMembership -> PartDefinition."""
    owner = package(name)
    child_id, membership_id = new_id(), new_id()
    child = {"@type": "PartDefinition", "@id": child_id, "declaredName": f"{name}Part",
             "owner": {"@id": owner["@id"]}, "owningRelationship": {"@id": membership_id}}
    membership = {"@type": "OwningMembership", "@id": membership_id,
                  "source": [{"@id": owner["@id"]}], "target": [{"@id": child_id}],
                  "owningRelatedElement": {"@id": owner["@id"]}, "ownedRelatedElement": [{"@id": child_id}],
                  "memberElement": {"@id": child_id}}
    return owner, membership, child


def test_roots_are_the_elements_without_owner(base_url, headers, project):
    pid = project["@id"]
    owner, membership, child = owned_model("A")
    other = package("Other")
    head = commit(base_url, headers, pid, [owner, membership, child, other])

    # the owner reference of the member arrives as it was sent
    served = get(base_url, headers, f"/projects/{pid}/commits/{head}/elements/{child['@id']}").json()
    assert served["@type"] == "PartDefinition"
    assert served["owner"] == {"@id": owner["@id"]}
    assert served["owningRelationship"] == {"@id": membership["@id"]}

    expected = {owner["@id"], other["@id"]}
    assert ids_of(get(base_url, headers, f"/projects/{pid}/commits/{head}/roots")) == expected
    # /root stays as an alias
    assert ids_of(get(base_url, headers, f"/projects/{pid}/commits/{head}/root")) == expected


def test_a_member_is_not_a_root_even_if_it_does_not_name_its_owner(base_url, headers, project):
    pid = project["@id"]
    owner, membership, child = owned_model("B")
    child.pop("owner")
    child.pop("owningRelationship")
    head = commit(base_url, headers, pid, [owner, membership, child])
    # the membership owns the child (ownedRelatedElement)
    assert ids_of(get(base_url, headers, f"/projects/{pid}/commits/{head}/roots")) == {owner["@id"]}


def test_roots_follow_the_state_of_the_commit(base_url, headers, project):
    pid = project["@id"]
    first = commit(base_url, headers, pid, [package("A"), package("B")])
    a_id = ids_of(get(base_url, headers, f"/projects/{pid}/commits/{first}/roots"))
    assert len(a_id) == 2
    gone = sorted(a_id)[0]
    second = commit(base_url, headers, pid, deletions=[gone])
    assert ids_of(get(base_url, headers, f"/projects/{pid}/commits/{second}/roots")) == a_id - {gone}
    assert ids_of(get(base_url, headers, f"/projects/{pid}/commits/{first}/roots")) == a_id


def test_roots_with_invalid_requests(base_url, headers, project):
    pid = project["@id"]
    assert get(base_url, headers, f"/projects/{pid}/commits/{uuid.uuid4()}/roots").status_code == 404
    assert get(base_url, headers, f"/projects/{pid}/commits/xyz/roots").status_code == 400
    assert get(base_url, headers, f"/projects/{uuid.uuid4()}/commits/{uuid.uuid4()}/roots").status_code == 404
    assert get(base_url, headers, f"/projects/{pid}/commits/{uuid.uuid4()}/root").status_code == 404
