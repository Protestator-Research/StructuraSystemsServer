"""A.1 ProjectService Conformance Test Cases (PIM-PS-001 ... 004)."""
import uuid

import pytest

from .conftest import all_record_ids
from .pim_client import new_id


@pytest.mark.conformance("PIM-PS-001", service="ProjectService", read=False)
@pytest.mark.parametrize("description", ["a description", "", None], ids=["description", "empty_description", "no_description"])
def test_pim_ps_001_create_project(pim, mongo, description):
    before = all_record_ids(mongo)
    name = f"ps001_{uuid.uuid4().hex[:8]}"
    result = pim.create_project(name, description) if description is not None else pim.create_project(name)
    assert result.status == 201, result.response.text
    project = result.value
    # 2. the id is a new UUID
    assert str(uuid.UUID(project["@id"])) == project["@id"]
    assert project["@id"] not in before
    # 3./4. a default branch with its own new id
    assert project["defaultBranch"] is not None
    branch_id = project["defaultBranch"]["@id"]
    assert branch_id not in before and branch_id != project["@id"]
    # 6./7. name and description as inputted
    assert project["name"] == name
    if description is not None:
        assert (project.get("description") or "") == description
    # 5. the default branch is called 'main'
    branch = pim.get_branch_by_id(project["@id"], branch_id)
    assert branch.status == 200, branch.response.text
    assert branch.value["name"] == "main"
    # the project is among all projects
    _, projects = pim.get_projects()
    assert project["@id"] in [p["@id"] for p in projects]


@pytest.mark.conformance("PIM-PS-002", service="ProjectService", read=True)
def test_pim_ps_002_get_projects(pim, mongo, project):
    from conftest import TEST_DB
    result, projects = pim.get_projects()
    assert result.status == 200
    ids = [p["@id"] for p in projects]
    assert project["@id"] in ids
    # all of the existing projects, each once
    assert len(ids) == len(set(ids))
    assert set(ids) == {document["_id"] for document in mongo[TEST_DB]["projects"].find({}, {"_id": 1})}


@pytest.mark.conformance("PIM-PS-003", service="ProjectService", read=True)
def test_pim_ps_003_get_project_by_id_exists(pim, project):
    result = pim.get_project_by_id(project["@id"])
    assert result.status == 200
    assert result.value == project


@pytest.mark.conformance("PIM-PS-004", service="ProjectService", read=True)
def test_pim_ps_004_get_project_by_id_does_not_exist(pim):
    result = pim.get_project_by_id(new_id())
    assert result.status == 404
    assert result.value is None
    assert result.communicates_error
