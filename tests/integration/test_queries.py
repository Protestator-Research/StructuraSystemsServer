"""QueryService: stored queries (CRUD, results) and ad-hoc queries (query-results)."""
import uuid

import pytest
import requests

from conftest import ADMIN_PASSWORD, MONGO_URI, TEST_DB, auth, login, start_server


@pytest.fixture
def headers(admin_token):
    return auth(admin_token)


def new_id():
    return str(uuid.uuid4())


def new_project(base_url, headers):
    response = requests.post(f"{base_url}/projects", json={"name": f"queries_{uuid.uuid4().hex[:8]}", "description": "queries"},
                             headers=headers, timeout=10)
    assert response.status_code == 201, response.text
    return response.json()


def commit(base_url, headers, project_id, payloads=(), deletions=()):
    change = [{"payload": payload} for payload in payloads]
    change += [{"identity": {"@id": identity}, "payload": None} for identity in deletions]
    response = requests.post(f"{base_url}/projects/{project_id}/commits", json={"description": "t", "change": change},
                             headers=headers, timeout=10)
    assert response.status_code == 201, response.text
    return response.json()["@id"]


def element(type_, name, **extra):
    return {"@type": type_, "@id": new_id(), "declaredName": name, **extra}


def primitive(prop, op, value, inverse=False):
    return {"@type": "PrimitiveConstraint", "property": prop, "operator": op, "value": value, "inverse": inverse}


def composite(op, *constraints):
    return {"@type": "CompositeConstraint", "operator": op, "constraint": list(constraints)}


@pytest.fixture
def model(base_url, headers):
    """alpha (mass 1) owns gamma (mass 9) and delta, a part definition (mass 2.5); beta has mass 4."""
    project = new_project(base_url, headers)
    alpha = element("Package", "alpha", mass=1, tags=["x", "y"])
    beta = element("Package", "beta", mass=4, tags=["y"])
    gamma = element("Package", "gamma", mass=9, owner={"@id": alpha["@id"]})
    delta = element("PartDefinition", "delta", mass=2.5, owner={"@id": alpha["@id"]})
    head = commit(base_url, headers, project["@id"], [alpha, beta, gamma, delta])
    return {"project": project["@id"], "commit": head, "alpha": alpha, "beta": beta, "gamma": gamma, "delta": delta}


def run(base_url, headers, model, query, commit_id="default", method="POST"):
    params = {} if commit_id is None else {"commitId": model["commit"] if commit_id == "default" else commit_id}
    response = requests.request(method, f"{base_url}/projects/{model['project']}/query-results", json=query, params=params,
                                headers=headers, timeout=10)
    assert response.status_code == 200, response.text
    return response.json()


def names(rows):
    return sorted(row.get("declaredName", "-") for row in rows)


def query(where=None, **extra):
    body = {"@type": "Query", **extra}
    if where is not None:
        body["where"] = where
    return body


# --- CRUD -----------------------------------------------------------------------------------------------------------


def test_query_lifecycle(base_url, headers):
    project = new_project(base_url, headers)
    pid = project["@id"]
    url = f"{base_url}/projects/{pid}/queries"
    assert requests.get(url, headers=headers, timeout=10).json() == []

    created = requests.post(url, json=query(primitive("@type", "=", "Package"), name="packages", select=["declaredName"],
                                            orderBy=["declaredName"]), headers=headers, timeout=10)
    assert created.status_code == 201, created.text
    stored = created.json()
    assert stored["@type"] == "Query"
    assert stored["name"] == "packages"
    assert stored["owningProject"] == {"@id": pid}
    assert stored["select"] == ["declaredName"]
    assert stored["orderBy"] == ["declaredName"]
    assert stored["where"]["property"] == "@type"
    qid = stored["@id"]

    assert requests.get(f"{url}/{qid}", headers=headers, timeout=10).json() == stored
    assert [q["@id"] for q in requests.get(url, headers=headers, timeout=10).json()] == [qid]

    replaced = requests.put(f"{url}/{qid}", json=query(primitive("@type", "=", "Part"), name="renamed"), headers=headers, timeout=10)
    assert replaced.status_code == 200, replaced.text
    assert replaced.json()["name"] == "renamed"
    assert replaced.json()["@id"] == qid
    assert replaced.json()["select"] == []
    assert replaced.json()["where"]["value"] == "Part"
    assert requests.get(f"{url}/{qid}", headers=headers, timeout=10).json() == replaced.json()

    deleted = requests.delete(f"{url}/{qid}", headers=headers, timeout=10)
    assert deleted.status_code == 200, deleted.text
    assert deleted.json()["@id"] == qid
    assert requests.get(f"{url}/{qid}", headers=headers, timeout=10).status_code == 404
    assert requests.delete(f"{url}/{qid}", headers=headers, timeout=10).status_code == 404
    assert requests.put(f"{url}/{qid}", json=query(name="x"), headers=headers, timeout=10).status_code == 404
    assert requests.get(url, headers=headers, timeout=10).json() == []


def test_query_with_scope_roundtrips_as_references(base_url, headers):
    pid = new_project(base_url, headers)["@id"]
    scope_id = new_id()
    created = requests.post(f"{base_url}/projects/{pid}/queries", json=query(scope=[{"@id": scope_id}]), headers=headers, timeout=10)
    assert created.status_code == 201, created.text
    assert created.json()["scope"] == [{"@id": scope_id}]


@pytest.mark.parametrize("body", [
    [1, 2],
    {"@type": "Commit"},
    {"@type": "Query", "where": {"@type": "PrimitiveConstraint", "property": "a", "operator": "~", "value": 1}},
    {"@type": "Query", "where": {"@type": "NoConstraint"}},
    {"@type": "Query", "where": {"@type": "CompositeConstraint", "operator": "and", "constraint": []}},
    {"@type": "Query", "select": [1]},
], ids=["array", "wrong_type", "bad_operator", "bad_constraint_type", "empty_composite", "select_not_strings"])
def test_invalid_query_returns_400(base_url, headers, body):
    pid = new_project(base_url, headers)["@id"]
    for method, path in (("POST", "queries"), ("POST", "query-results"), ("GET", "query-results")):
        response = requests.request(method, f"{base_url}/projects/{pid}/{path}", json=body, headers=headers, timeout=10)
        assert response.status_code == 400, f"{method} {path}: {response.text}"
    created = requests.post(f"{base_url}/projects/{pid}/queries", json=query(name="ok"), headers=headers, timeout=10).json()
    assert requests.put(f"{base_url}/projects/{pid}/queries/{created['@id']}", json=body, headers=headers, timeout=10).status_code == 400


def test_query_without_body_or_with_bad_json_returns_400(base_url, headers):
    pid = new_project(base_url, headers)["@id"]
    for path in ("queries", "query-results"):
        assert requests.post(f"{base_url}/projects/{pid}/{path}", headers=headers, timeout=10).status_code == 400
        assert requests.post(f"{base_url}/projects/{pid}/{path}", data="{bad", timeout=10,
                             headers={**headers, "Content-Type": "application/json"}).status_code == 400


def test_query_routes_with_invalid_ids_and_access(base_url, headers, make_login):
    pid = new_project(base_url, headers)["@id"]
    qid = requests.post(f"{base_url}/projects/{pid}/queries", json=query(name="q"), headers=headers, timeout=10).json()["@id"]
    assert requests.get(f"{base_url}/projects/{pid}/queries/xyz", headers=headers, timeout=10).status_code == 400
    assert requests.get(f"{base_url}/projects/{pid}/queries/{uuid.uuid4()}", headers=headers, timeout=10).status_code == 404
    assert requests.get(f"{base_url}/projects/{pid}/queries/{uuid.uuid4()}/results", headers=headers, timeout=10).status_code == 404
    assert requests.get(f"{base_url}/projects/{pid}/queries/xyz/results", headers=headers, timeout=10).status_code == 400
    assert requests.get(f"{base_url}/projects/xyz/queries", headers=headers, timeout=10).status_code == 400

    # a query of another project is unknown here
    other = new_project(base_url, headers)["@id"]
    assert requests.get(f"{base_url}/projects/{other}/queries/{qid}", headers=headers, timeout=10).status_code == 404
    assert requests.delete(f"{base_url}/projects/{other}/queries/{qid}", headers=headers, timeout=10).status_code == 404

    _, token = make_login()
    stranger = auth(token)
    for call in (requests.get(f"{base_url}/projects/{pid}/queries", headers=stranger, timeout=10),
                 requests.post(f"{base_url}/projects/{pid}/queries", json=query(name="x"), headers=stranger, timeout=10),
                 requests.put(f"{base_url}/projects/{pid}/queries/{qid}", json=query(name="x"), headers=stranger, timeout=10),
                 requests.delete(f"{base_url}/projects/{pid}/queries/{qid}", headers=stranger, timeout=10),
                 requests.get(f"{base_url}/projects/{pid}/queries/{qid}/results", headers=stranger, timeout=10),
                 requests.post(f"{base_url}/projects/{pid}/query-results", json=query(), headers=stranger, timeout=10)):
        assert call.status_code == 404


def test_deleting_a_project_deletes_its_queries(base_url, headers, mongo):
    pid = new_project(base_url, headers)["@id"]
    for index in range(2):
        assert requests.post(f"{base_url}/projects/{pid}/queries", json=query(name=f"q{index}"), headers=headers, timeout=10).status_code == 201
    database = mongo[TEST_DB]
    assert database["queries"].count_documents({"_project_id": pid}) == 2
    assert requests.delete(f"{base_url}/projects/{pid}", headers=headers, timeout=10).status_code == 200
    assert database["queries"].count_documents({"_project_id": pid}) == 0
    # and the project itself is gone from the database (it used to stay and came back at the next start)
    assert database["projects"].count_documents({"_id": pid}) == 0


def test_queries_are_loaded_at_start(mongo):
    db_name, port = "structura_systems_test_queries", 18092
    base = f"http://127.0.0.1:{port}"
    with start_server(db_name, port) as (proc, output):
        token = login(base, "admin", ADMIN_PASSWORD)
        pid = new_project(base, auth(token))["@id"]
        created = requests.post(f"{base}/projects/{pid}/queries", json=query(primitive("@type", "=", "Package"), name="kept"),
                                headers=auth(token), timeout=10).json()
        gone = requests.post(f"{base}/projects/{pid}/queries", json=query(name="gone"), headers=auth(token), timeout=10).json()
        assert requests.delete(f"{base}/projects/{pid}/queries/{gone['@id']}", headers=auth(token), timeout=10).status_code == 200
        replaced = requests.put(f"{base}/projects/{pid}/queries/{created['@id']}", json=query(primitive("@type", "=", "Part"), name="kept 2"),
                                headers=auth(token), timeout=10).json()
    try:
        with start_server(db_name, port, drop_db=False):
            token = login(base, "admin", ADMIN_PASSWORD)
            listed = requests.get(f"{base}/projects/{pid}/queries", headers=auth(token), timeout=10).json()
            assert listed == [replaced]
            assert requests.get(f"{base}/projects/{pid}/queries/{gone['@id']}", headers=auth(token), timeout=10).status_code == 404
    finally:
        from pymongo import MongoClient
        MongoClient(MONGO_URI).drop_database(db_name)


# --- results --------------------------------------------------------------------------------------------------------


def test_results_of_a_stored_query(base_url, headers, model):
    pid = model["project"]
    stored = requests.post(f"{base_url}/projects/{pid}/queries", json=query(primitive("@type", "=", "Package"), orderBy=["declaredName"]),
                           headers=headers, timeout=10).json()
    url = f"{base_url}/projects/{pid}/queries/{stored['@id']}/results"
    rows = requests.get(url, params={"commitId": model["commit"]}, headers=headers, timeout=10)
    assert rows.status_code == 200, rows.text
    assert [row["declaredName"] for row in rows.json()] == ["alpha", "beta", "gamma"]

    # without commitId the head of the default branch is used
    assert requests.get(url, headers=headers, timeout=10).json() == rows.json()
    later = commit(base_url, headers, pid, [element("Package", "omega")])
    assert [row["declaredName"] for row in requests.get(url, headers=headers, timeout=10).json()][-1] == "omega"
    # an earlier commit shows the earlier state
    assert len(requests.get(url, params={"commitId": model["commit"]}, headers=headers, timeout=10).json()) == 3
    assert len(requests.get(url, params={"commitId": later}, headers=headers, timeout=10).json()) == 4

    assert requests.get(url, params={"commitId": str(uuid.uuid4())}, headers=headers, timeout=10).status_code == 404
    assert requests.get(url, params={"commitId": "xyz"}, headers=headers, timeout=10).status_code == 400


def test_results_without_a_head_are_empty(base_url, headers):
    pid = new_project(base_url, headers)["@id"]
    stored = requests.post(f"{base_url}/projects/{pid}/queries", json=query(name="empty"), headers=headers, timeout=10).json()
    assert requests.get(f"{base_url}/projects/{pid}/queries/{stored['@id']}/results", headers=headers, timeout=10).json() == []
    response = requests.post(f"{base_url}/projects/{pid}/query-results", json=query(), headers=headers, timeout=10)
    assert response.status_code == 200
    assert response.json() == []


def test_results_do_not_contain_deleted_data(base_url, headers, model):
    pid = model["project"]
    after = commit(base_url, headers, pid, deletions=[model["beta"]["@id"]])
    assert "beta" not in names(run(base_url, headers, model, query(), after))
    assert "beta" in names(run(base_url, headers, model, query(), model["commit"]))


def test_query_results_by_get_and_post(base_url, headers, model):
    body = query(primitive("declaredName", "=", "alpha"))
    assert names(run(base_url, headers, model, body, method="POST")) == ["alpha"]
    assert names(run(base_url, headers, model, body, method="GET")) == ["alpha"]
    assert names(run(base_url, headers, model, body, commit_id=None)) == ["alpha"]
    unknown = requests.post(f"{base_url}/projects/{model['project']}/query-results", json=body, params={"commitId": str(uuid.uuid4())},
                            headers=headers, timeout=10)
    assert unknown.status_code == 404


# --- evaluation -----------------------------------------------------------------------------------------------------


@pytest.mark.parametrize("where, expected", [
    (primitive("@type", "=", "Package"), ["alpha", "beta", "gamma"]),
    (primitive("declaredName", "=", "delta"), ["delta"]),
    (primitive("mass", "=", 4), ["beta"]),
    (primitive("mass", "<", 4), ["alpha", "delta"]),
    (primitive("mass", "<=", 4), ["alpha", "beta", "delta"]),
    (primitive("mass", ">", 4), ["gamma"]),
    (primitive("mass", ">=", 4), ["beta", "gamma"]),
    (primitive("mass", "in", [1, 9]), ["alpha", "gamma"]),
    (primitive("@type", "in", ["PartDefinition", "Nope"]), ["delta"]),
    (primitive("@type", "instanceOf", "PartDefinition"), ["delta"]),
    (primitive("@type", "instanceOf", "Element"), ["alpha", "beta", "delta", "gamma"]),
    (primitive("tags", "=", "x"), ["alpha"]),
    (primitive("tags", "=", "y"), ["alpha", "beta"]),
    (primitive("mass", "<", 4, inverse=True), ["beta", "gamma"]),
    (primitive("@type", "=", "Package", inverse=True), ["delta"]),
    (composite("and", primitive("@type", "=", "Package"), primitive("mass", ">", 3)), ["beta", "gamma"]),
    (composite("or", primitive("mass", ">", 8), primitive("mass", "<", 2)), ["alpha", "gamma"]),
    (composite("and", primitive("@type", "=", "Package"), composite("or", primitive("mass", ">", 8), primitive("mass", "<", 2))), ["alpha", "gamma"]),
], ids=["type", "name", "eq", "lt", "le", "gt", "ge", "in", "type_in", "instanceOf", "instanceOf_element", "array_x", "array_y",
        "inverse", "inverse_type", "and", "or", "nested"])
def test_operators_and_constraints(base_url, headers, model, where, expected):
    assert names(run(base_url, headers, model, query(where))) == expected


def test_reference_properties_are_compared_by_id(base_url, headers, model):
    rows = run(base_url, headers, model, query(primitive("owner", "=", model["alpha"]["@id"])))
    assert names(rows) == ["delta", "gamma"]
    assert names(run(base_url, headers, model, query(primitive("@id", "=", model["beta"]["@id"])))) == ["beta"]


def test_scope_limits_the_query_to_the_data_and_what_they_own(base_url, headers, model):
    alpha, beta = model["alpha"]["@id"], model["beta"]["@id"]
    assert names(run(base_url, headers, model, query(scope=[{"@id": alpha}]))) == ["alpha", "delta", "gamma"]
    assert names(run(base_url, headers, model, query(scope=[{"@id": beta}]))) == ["beta"]
    assert names(run(base_url, headers, model, query(primitive("@type", "=", "Package"), scope=[{"@id": alpha}, {"@id": beta}]))) == [
        "alpha", "beta", "gamma"]
    assert run(base_url, headers, model, query(scope=[{"@id": new_id()}])) == []


def test_select_keeps_id_and_type(base_url, headers, model):
    rows = run(base_url, headers, model, query(primitive("declaredName", "=", "beta"), select=["mass"]))
    assert len(rows) == 1
    assert set(rows[0]) == {"@id", "@type", "mass"}
    assert rows[0]["mass"] == 4
    full = run(base_url, headers, model, query(primitive("declaredName", "=", "beta")))
    assert "declaredName" in full[0]


def test_order_by_properties(base_url, headers, model):
    def ordered(*order_by):
        return [row["declaredName"] for row in run(base_url, headers, model, query(orderBy=list(order_by)))]

    assert ordered("mass") == ["alpha", "delta", "beta", "gamma"]
    assert ordered("declaredName") == ["alpha", "beta", "delta", "gamma"]
    assert ordered("@type", "mass") == ["alpha", "beta", "gamma", "delta"]


# --- ProjectUsage and ExternalRelationship (PU-001, ER-001..003) ---------------------------------------------------------


def test_project_usage_is_found_by_type(base_url, headers):
    used = new_project(base_url, headers)["@id"]
    used_commit = commit(base_url, headers, used, [element("Package", "Library")])
    pid = new_project(base_url, headers)["@id"]
    usage = {"@type": "ProjectUsage", "@id": new_id(), "usedProjectCommit": {"@id": used_commit}, "usedProject": {"@id": used}}
    head = commit(base_url, headers, pid, [usage, element("Package", "Other")])
    model = {"project": pid, "commit": head}

    rows = run(base_url, headers, model, query(primitive("@type", "=", "ProjectUsage")))
    assert [row["@id"] for row in rows] == [usage["@id"]]
    assert rows[0]["usedProjectCommit"] == {"@id": used_commit}
    # getProjectUsages through a stored query
    stored = requests.post(f"{base_url}/projects/{pid}/queries", json=query(primitive("@type", "=", "ProjectUsage")), headers=headers, timeout=10).json()
    results = requests.get(f"{base_url}/projects/{pid}/queries/{stored['@id']}/results", params={"commitId": head}, headers=headers, timeout=10)
    assert [row["@id"] for row in results.json()] == [usage["@id"]]
    assert names(run(base_url, headers, model, query(primitive("usedProjectCommit", "=", used_commit)))) == ["-"]


def test_external_relationships_by_type_element_and_id(base_url, headers):
    pid = new_project(base_url, headers)["@id"]
    target = element("Package", "Target")
    other = element("Package", "Unrelated")
    external = {"@type": "ExternalData", "@id": new_id(), "resourceIdentifier": "https://example.com/data"}
    first = {"@type": "ExternalRelationship", "@id": new_id(), "elementEnd": {"@id": target["@id"]},
             "externalDataEnd": {"@id": external["@id"]}, "specification": "select 1", "language": "SQL"}
    second = {"@type": "ExternalRelationship", "@id": new_id(), "elementEnd": {"@id": other["@id"]},
              "externalDataEnd": {"@id": external["@id"]}}
    head = commit(base_url, headers, pid, [target, other, external, first, second])
    model = {"project": pid, "commit": head}

    by_type = run(base_url, headers, model, query(primitive("@type", "=", "ExternalRelationship")))
    assert sorted(row["@id"] for row in by_type) == sorted([first["@id"], second["@id"]])

    by_element = run(base_url, headers, model, query(composite(
        "and", primitive("@type", "=", "ExternalRelationship"), primitive("elementEnd", "=", target["@id"]))))
    assert [row["@id"] for row in by_element] == [first["@id"]]
    assert by_element[0]["language"] == "SQL"

    by_id = run(base_url, headers, model, query(composite(
        "and", primitive("@type", "=", "ExternalRelationship"), primitive("@id", "=", second["@id"]))))
    assert [row["@id"] for row in by_id] == [second["@id"]]

    # a deleted external relationship is no longer found
    after = commit(base_url, headers, pid, deletions=[first["@id"]])
    remaining = run(base_url, headers, model, query(primitive("@type", "=", "ExternalRelationship")), after)
    assert [row["@id"] for row in remaining] == [second["@id"]]
