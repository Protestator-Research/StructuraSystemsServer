"""A.3 ProjectDataVersioningService Conformance Test Cases (PIM-PCB-001 ... 015)."""
import uuid

import pytest

from .conftest import all_record_ids, package, payload_change
from .pim_client import new_id


@pytest.fixture
def branch(pim, model):
    """A second branch whose head is the commit of the model."""
    created = pim.create_branch(model["project"]["@id"], "dev", model["commit"]["@id"])
    assert created.status == 201, created.response.text
    return created.value


@pytest.mark.conformance("PIM-PCB-001", service="ProjectDataVersioningService", read=False)
def test_pim_pcb_001_create_branch(pim, mongo, model):
    pid, head = model["project"]["@id"], model["commit"]["@id"]
    before = all_record_ids(mongo)
    result = pim.create_branch(pid, "feature", head)
    assert result.status == 201, result.response.text
    branch = result.value
    assert str(uuid.UUID(branch["@id"])) == branch["@id"] and branch["@id"] not in before
    assert branch["owningProject"] == {"@id": pid}
    assert branch["head"] == {"@id": head}
    assert branch["name"] == "feature"
    # the branch is a branch of the project
    assert branch["@id"] in [b["@id"] for b in pim.get_branches(pid).value]


@pytest.mark.conformance("PIM-PCB-002", service="ProjectDataVersioningService", read=True)
def test_pim_pcb_002_get_branches(pim, model, branch):
    pid = model["project"]["@id"]
    result = pim.get_branches(pid)
    assert result.status == 200
    assert sorted(b["@id"] for b in result.value) == sorted([model["project"]["defaultBranch"]["@id"], branch["@id"]])
    assert all(b["owningProject"] == {"@id": pid} for b in result.value)


@pytest.mark.conformance("PIM-PCB-003", service="ProjectDataVersioningService", read=True)
def test_pim_pcb_003_get_branch_by_id(pim, model, branch):
    result = pim.get_branch_by_id(model["project"]["@id"], branch["@id"])
    assert result.status == 200
    assert result.value == branch


@pytest.mark.conformance("PIM-PCB-004", service="ProjectDataVersioningService", read=True)
def test_pim_pcb_004_get_branch_by_id_does_not_exist(pim, model):
    result = pim.get_branch_by_id(model["project"]["@id"], new_id())
    assert result.status == 404 and result.value is None and result.communicates_error


@pytest.mark.conformance("PIM-PCB-005", service="ProjectDataVersioningService", read=False)
def test_pim_pcb_005_delete_branch(pim, model, branch):
    pid = model["project"]["@id"]
    result = pim.delete_branch(pid, branch["@id"])
    assert result.status == 200
    assert result.value["@id"] == branch["@id"]
    assert pim.get_branch_by_id(pid, branch["@id"]).status == 404
    assert branch["@id"] not in [b["@id"] for b in pim.get_branches(pid).value]


@pytest.mark.conformance("PIM-PCB-006", service="ProjectDataVersioningService", read=False)
def test_pim_pcb_006_delete_branch_does_not_exist(pim, model, other_model):
    pid = model["project"]["@id"]
    unknown = pim.delete_branch(pid, new_id())
    assert unknown.status == 404 and unknown.value is None and unknown.communicates_error
    # a branch of another project does not exist in this one, and is not deleted
    foreign = other_model["project"]["defaultBranch"]["@id"]
    assert pim.delete_branch(pid, foreign).status == 404
    assert pim.get_branch_by_id(other_model["project"]["@id"], foreign).status == 200


@pytest.mark.conformance("PIM-PCB-007", service="ProjectDataVersioningService", read=True)
def test_pim_pcb_007_get_default_branch(pim, model):
    result = pim.get_default_branch(model["project"]["@id"])
    assert result.status == 200
    assert result.value["@id"] == model["project"]["defaultBranch"]["@id"]
    assert result.value["name"] == "main"


@pytest.mark.conformance("PIM-PCB-008", service="ProjectDataVersioningService", read=False)
def test_pim_pcb_008_set_default_branch(pim, model, branch):
    pid = model["project"]["@id"]
    result = pim.set_default_branch(pid, branch["@id"])
    assert result.status == 200
    assert result.value["@id"] == pid
    assert result.value["defaultBranch"] == {"@id": branch["@id"]}
    assert pim.get_default_branch(pid).value["@id"] == branch["@id"]


@pytest.mark.conformance("PIM-PCB-009", service="ProjectDataVersioningService", read=False)
def test_pim_pcb_009_set_default_branch_does_not_exist(pim, model, other_model):
    pid = model["project"]["@id"]
    original = model["project"]["defaultBranch"]
    for branch_id in (new_id(), other_model["project"]["defaultBranch"]["@id"]):
        result = pim.set_default_branch(pid, branch_id)
        assert result.status == 404 and result.communicates_error
        assert pim.get_project_by_id(pid).value["defaultBranch"] == original


@pytest.mark.conformance("PIM-PCB-010", service="ProjectDataVersioningService", read=False)
def test_pim_pcb_010_create_commit(pim, mongo, model, branch):
    pid, first = model["project"]["@id"], model["commit"]["@id"]
    before = all_record_ids(mongo)
    new_element = package("new")
    change = payload_change(new_element)

    # The head of the default branch moves on, so the commit on the side branch and the head diverge. (A requested previous commit that
    # is an ancestor of another one is dropped, because it adds nothing; see the test for redundant previous commits in test_versioning_api.)
    main_head = pim.create_commit(pid, payload_change(package("main only")))
    assert main_head.status == 201
    main_head = main_head.value["@id"]
    side = pim.create_commit(pid, payload_change(package("side")), branch_id=branch["@id"])
    assert side.status == 201
    result = pim.create_commit(pid, change, previous_commits=[side.value["@id"]])  # the default branch, whose head is main_head
    assert result.status == 201, result.response.text
    commit = result.value
    assert commit["@id"] not in before and str(uuid.UUID(commit["@id"])) == commit["@id"]
    assert commit["owningProject"] == {"@id": pid}
    # the change is as inputted: the payload, with an own id for the data version
    assert [v["payload"]["@id"] for v in commit["change"]] == [new_element["@id"]]
    assert commit["change"][0]["@id"] not in before
    # previousCommits is the head of the branch united with the inputted ones
    assert sorted(p["@id"] for p in commit["previousCommits"]) == sorted([main_head, side.value["@id"]])
    # the version includes the new change, and what was there before
    elements = {e["@id"] for e in pim.get_elements(pid, commit["@id"]).value}
    assert new_element["@id"] in elements and model["a"]["@id"] in elements
    # the head of the branch is the created commit
    assert pim.get_default_branch(pid).value["head"] == {"@id": commit["@id"]}
    assert pim.get_branch_by_id(pid, branch["@id"]).value["head"] == {"@id": side.value["@id"]}

    # with a given branch the head of that branch moves, the others do not
    on_dev = pim.create_commit(pid, payload_change(package("dev")), branch_id=branch["@id"])
    assert on_dev.status == 201
    assert on_dev.value["previousCommits"] == [{"@id": side.value["@id"]}]
    assert pim.get_branch_by_id(pid, branch["@id"]).value["head"] == {"@id": on_dev.value["@id"]}
    assert pim.get_default_branch(pid).value["head"] == {"@id": commit["@id"]}


@pytest.mark.conformance("PIM-PCB-011", service="ProjectDataVersioningService", read=True)
def test_pim_pcb_011_get_commit_by_id(pim, model):
    result = pim.get_commit_by_id(model["project"]["@id"], model["commit"]["@id"])
    assert result.status == 200
    assert result.value == model["commit"]


@pytest.mark.conformance("PIM-PCB-012", service="ProjectDataVersioningService", read=True)
def test_pim_pcb_012_get_commit_by_id_does_not_exist(pim, model, other_model):
    pid = model["project"]["@id"]
    result = pim.get_commit_by_id(pid, new_id())
    assert result.status == 404 and result.value is None and result.communicates_error
    # a commit of another project does not exist in this one
    assert pim.get_commit_by_id(pid, other_model["commit"]["@id"]).status == 404


@pytest.mark.conformance("PIM-PCB-013", service="ProjectDataVersioningService", read=True)
def test_pim_pcb_013_get_head(pim, model, branch):
    pid = model["project"]["@id"]
    result = pim.get_head(pid, branch["@id"])
    assert result.status == 200
    assert result.value["@id"] == model["commit"]["@id"]
    # a project without a commit has a default branch without head
    empty = pim.create_project(f"empty_{uuid.uuid4().hex[:8]}", "x").value
    assert pim.get_default_branch(empty["@id"]).value["head"] is None


@pytest.mark.conformance("PIM-PCB-014", service="ProjectDataVersioningService", read=True)
def test_pim_pcb_014_get_head_branch_does_not_exist(pim, model):
    result = pim.get_head(model["project"]["@id"], new_id())
    assert result.status == 404 and result.value is None and result.communicates_error


@pytest.mark.conformance("PIM-PCB-015", service="ProjectDataVersioningService", read=True)
def test_pim_pcb_015_get_head_branch_not_in_project(pim, model, other_model):
    """The specification says 'the provided input is invalid'. The server answers 404 like for an unknown branch (plan decision):
    the access control must not allow listing resources of other projects, so a foreign branch is 'not there'."""
    result = pim.get_head(model["project"]["@id"], other_model["project"]["defaultBranch"]["@id"])
    assert result.status == 404 and result.value is None and result.communicates_error
