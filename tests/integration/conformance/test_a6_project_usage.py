"""A.6 ProjectUsageService Conformance Test Cases (PIM-PU-001). Table 10 maps get_project_usages to a query on @type."""
import pytest

from .conftest import package, payload_change
from .pim_client import new_id


@pytest.mark.conformance("PIM-PU-001", service="ProjectUsageService", read=True)
def test_pim_pu_001_get_project_usages(pim, project, other_model):
    pid = project["@id"]
    usage = {"@type": "ProjectUsage", "@id": new_id(), "usedProjectCommit": {"@id": other_model["commit"]["@id"]},
             "usedProject": {"@id": other_model["project"]["@id"]}}
    commit = pim.create_commit(pid, payload_change(usage, package("other data"))).value
    result = pim.get_project_usages(pid, commit["@id"])
    assert result.status == 200
    assert [u["@id"] for u in result.value] == [usage["@id"]]
    assert result.value[0]["usedProjectCommit"] == {"@id": other_model["commit"]["@id"]}
    assert result.value[0]["usedProject"] == {"@id": other_model["project"]["@id"]}
    # none at a commit without usage
    assert pim.get_project_usages(other_model["project"]["@id"], other_model["commit"]["@id"]).value == []
    # a usage can be removed again
    removed = pim.create_commit(pid, [{"identity": {"@id": usage["@id"]}, "payload": None}]).value
    assert pim.get_project_usages(pid, removed["@id"]).value == []
    assert len(pim.get_project_usages(pid, commit["@id"]).value) == 1
