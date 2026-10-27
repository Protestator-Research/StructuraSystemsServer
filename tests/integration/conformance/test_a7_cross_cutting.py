"""A.7 Cross-Cutting Conformance Test Cases (PIM-CC-001 ... 007).

The PIM cases call an operation with a missing, unknown or foreign input. In the REST/HTTP PSM:
- an input that is missing from the path is an empty path segment (the server answers 400 for it),
- an input that is missing from the body (name, head, defaultBranch) is left out of the body or has no @id,
- 'does not exist' is 404 and 'required input' is 400, and the answer has a JSON body {"error": ...} and no result.
"""
import pytest

from .pim_client import MISSING, new_id

# Every operation as a function of (project, commit, branch, element) ids, returning (method, path, body). An empty string is a
# missing path segment.


def _elements(p, c, b, e):
    return "GET", f"/projects/{p}/commits/{c}/elements", None


def _element_by_id(p, c, b, e):
    return "GET", f"/projects/{p}/commits/{c}/elements/{e}", None


def _by_source(p, c, b, e):
    return "GET", f"/projects/{p}/commits/{c}/elements/{e}/relationships?direction=out", None


def _by_target(p, c, b, e):
    return "GET", f"/projects/{p}/commits/{c}/elements/{e}/relationships?direction=in", None


def _create_branch(p, c, b, e):
    return "POST", f"/projects/{p}/branches", {"@type": "Branch", "name": "cc", "head": {"@id": c}}


def _get_branches(p, c, b, e):
    return "GET", f"/projects/{p}/branches", None


def _get_branch_by_id(p, c, b, e):
    return "GET", f"/projects/{p}/branches/{b}", None


def _delete_branch(p, c, b, e):
    return "DELETE", f"/projects/{p}/branches/{b}", None


def _get_default_branch(p, c, b, e):
    return "GET", f"/projects/{p}", None


def _set_default_branch(p, c, b, e):
    return "PUT", f"/projects/{p}", {"@type": "Project", "defaultBranch": {"@id": b}}


def _create_commit(p, c, b, e):
    return "POST", f"/projects/{p}/commits", {"@type": "Commit", "change": [{"payload": {"@type": "Package", "@id": new_id(), "declaredName": "cc"}}]}


def _get_commit_by_id(p, c, b, e):
    return "GET", f"/projects/{p}/commits/{c}", None


def _get_head(p, c, b, e):
    return "GET", f"/projects/{p}/branches/{b}", None


def _create_query(p, c, b, e):
    return "POST", f"/projects/{p}/queries", {"@type": "Query"}


def _get_project_by_id(p, c, b, e):
    return "GET", f"/projects/{p}", None


OPERATIONS_WITH_PROJECT = [_elements, _element_by_id, _by_source, _by_target, _create_branch, _get_branches, _get_branch_by_id,
                           _delete_branch, _get_default_branch, _set_default_branch, _create_commit, _get_commit_by_id, _get_head,
                           _create_query]
OPERATIONS_WITH_COMMIT = [_elements, _element_by_id, _by_source, _by_target]
OPERATIONS_WITH_ELEMENT_ID = [_element_by_id, _by_source, _by_target]
OPERATIONS_WITH_BRANCH_ID = [_get_branch_by_id, _delete_branch, _get_head]


def names(operations):
    return [operation.__name__.lstrip("_") for operation in operations]


def assert_no_result(pim, call, status):
    method, path, body = call
    result = pim._call(method, path, json=body)
    assert result.status == status, f"{method} {path}: {result.status} {result.response.text[:200]}"
    assert result.value is None
    assert result.communicates_error, f"{method} {path}: no JSON error body: {result.response.text[:200]}"


@pytest.fixture
def ids(model):
    return {"p": model["project"]["@id"], "c": model["commit"]["@id"], "b": model["project"]["defaultBranch"]["@id"], "e": model["a"]["@id"]}


@pytest.mark.conformance("PIM-CC-001", service="CrossCutting", read=True)
@pytest.mark.parametrize("operation", OPERATIONS_WITH_PROJECT, ids=names(OPERATIONS_WITH_PROJECT))
def test_pim_cc_001_missing_project(pim, ids, operation):
    assert_no_result(pim, operation("", ids["c"], ids["b"], ids["e"]), 400)


@pytest.mark.conformance("PIM-CC-002", service="CrossCutting", read=True)
@pytest.mark.parametrize("operation", OPERATIONS_WITH_COMMIT, ids=names(OPERATIONS_WITH_COMMIT))
def test_pim_cc_002_missing_commit(pim, ids, operation):
    assert_no_result(pim, operation(ids["p"], "", ids["b"], ids["e"]), 400)


@pytest.mark.conformance("PIM-CC-002", service="CrossCutting", read=True)
def test_pim_cc_002_missing_commit_create_branch_head(pim, ids):
    result = pim.create_branch(ids["p"], "cc")  # no head in the body
    assert result.status == 400 and result.value is None and result.communicates_error
    result = pim._call("POST", f"/projects/{ids['p']}/branches", {"@type": "Branch", "name": "cc", "head": {}})
    assert result.status == 400 and result.communicates_error


@pytest.mark.conformance("PIM-CC-003", service="CrossCutting", read=True)
@pytest.mark.parametrize("operation", OPERATIONS_WITH_PROJECT, ids=names(OPERATIONS_WITH_PROJECT))
def test_pim_cc_003_project_does_not_exist(pim, ids, operation):
    assert_no_result(pim, operation(new_id(), ids["c"], ids["b"], ids["e"]), 404)


@pytest.mark.conformance("PIM-CC-004", service="CrossCutting", read=True)
@pytest.mark.parametrize("operation", OPERATIONS_WITH_COMMIT, ids=names(OPERATIONS_WITH_COMMIT))
def test_pim_cc_004_commit_does_not_exist(pim, ids, operation):
    assert_no_result(pim, operation(ids["p"], new_id(), ids["b"], ids["e"]), 404)


@pytest.mark.conformance("PIM-CC-004", service="CrossCutting", read=True)
def test_pim_cc_004_commit_does_not_exist_create_branch_head(pim, ids):
    result = pim.create_branch(ids["p"], "cc", new_id())
    assert result.status == 404 and result.value is None and result.communicates_error


@pytest.mark.conformance("PIM-CC-005", service="CrossCutting", read=True)
@pytest.mark.parametrize("operation", OPERATIONS_WITH_COMMIT, ids=names(OPERATIONS_WITH_COMMIT))
def test_pim_cc_005_commit_not_owned_by_project(pim, ids, other_model, operation):
    """The specification says 'the provided input is invalid'. The server answers 404, like for a commit that does not exist (plan
    decision): the access control must not allow listing resources of other projects, so a foreign commit is 'not there'."""
    assert_no_result(pim, operation(ids["p"], other_model["commit"]["@id"], ids["b"], ids["e"]), 404)


@pytest.mark.conformance("PIM-CC-005", service="CrossCutting", read=True)
def test_pim_cc_005_commit_not_owned_by_project_create_branch_head(pim, ids, other_model):
    """404 instead of the specified 'invalid input', see test_pim_cc_005_commit_not_owned_by_project."""
    result = pim.create_branch(ids["p"], "cc", other_model["commit"]["@id"])
    assert result.status == 404 and result.value is None and result.communicates_error
    # nothing was created
    assert [b["name"] for b in pim.get_branches(ids["p"]).value] == ["main"]


@pytest.mark.conformance("PIM-CC-006", service="CrossCutting", read=False)
@pytest.mark.parametrize("name", [MISSING, "", "   ", None, 5], ids=["missing", "empty", "blank", "null", "number"])
def test_pim_cc_006_missing_name_create_project(pim, name):
    result = pim.create_project(name)
    assert result.status == 400 and result.value is None and result.communicates_error


@pytest.mark.conformance("PIM-CC-006", service="CrossCutting", read=False)
@pytest.mark.parametrize("name", [MISSING, "", None], ids=["missing", "empty", "null"])
def test_pim_cc_006_missing_name_create_branch(pim, ids, name):
    result = pim.create_branch(ids["p"], name, ids["c"])
    assert result.status == 400 and result.value is None and result.communicates_error
    assert len(pim.get_branches(ids["p"]).value) == 1


@pytest.mark.conformance("PIM-CC-007", service="CrossCutting", read=True)
def test_pim_cc_007_missing_uuid_project_id(pim):
    assert_no_result(pim, _get_project_by_id("", "", "", ""), 400)


@pytest.mark.conformance("PIM-CC-007", service="CrossCutting", read=True)
@pytest.mark.parametrize("operation", OPERATIONS_WITH_ELEMENT_ID, ids=names(OPERATIONS_WITH_ELEMENT_ID))
def test_pim_cc_007_missing_uuid_element_id(pim, ids, operation):
    assert_no_result(pim, operation(ids["p"], ids["c"], ids["b"], ""), 400)


@pytest.mark.conformance("PIM-CC-007", service="CrossCutting", read=True)
@pytest.mark.parametrize("operation", OPERATIONS_WITH_BRANCH_ID, ids=names(OPERATIONS_WITH_BRANCH_ID))
def test_pim_cc_007_missing_uuid_branch_id(pim, ids, operation):
    assert_no_result(pim, operation(ids["p"], ids["c"], "", ids["e"]), 400)


@pytest.mark.conformance("PIM-CC-007", service="CrossCutting", read=True)
def test_pim_cc_007_missing_uuid_set_default_branch(pim, ids):
    """A PUT without defaultBranch changes nothing and is no error; a defaultBranch without @id is a missing branch id."""
    result = pim.set_default_branch(ids["p"], "")
    assert result.status == 400 and result.value is None and result.communicates_error


@pytest.mark.conformance("PIM-CC-007", service="CrossCutting", read=True)
def test_pim_cc_007_missing_uuid_commit_id(pim, ids):
    assert_no_result(pim, _get_commit_by_id(ids["p"], "", ids["b"], ids["e"]), 400)
