"""A.5 ExternalRelationshipService Conformance Test Cases (PIM-ER-001 ... 003). Table 10 maps the operations to queries on @type."""
import pytest

from .conftest import package, payload_change
from .pim_client import new_id


@pytest.fixture
def external(pim, project):
    element = package("target")
    data = {"@type": "ExternalData", "@id": new_id(), "resourceIdentifier": "https://example.com/data"}
    first = {"@type": "ExternalRelationship", "@id": new_id(), "elementEnd": {"@id": element["@id"]}, "externalDataEnd": {"@id": data["@id"]},
             "specification": "select 1", "language": "SQL"}
    second = {"@type": "ExternalRelationship", "@id": new_id(), "elementEnd": {"@id": element["@id"]}, "externalDataEnd": {"@id": data["@id"]}}
    commit = pim.create_commit(project["@id"], payload_change(element, data, first, second))
    assert commit.status == 201, commit.response.text
    return {"project": project, "commit": commit.value, "first": first, "second": second, "element": element}


@pytest.mark.conformance("PIM-ER-001", service="ExternalRelationshipService", read=False)
def test_pim_er_001_get_external_relationships(pim, external):
    pid, cid = external["project"]["@id"], external["commit"]["@id"]
    result = pim.get_external_relationships(pid, cid)
    assert result.status == 200
    assert sorted(r["@id"] for r in result.value) == sorted([external["first"]["@id"], external["second"]["@id"]])
    assert all(r["@type"] == "ExternalRelationship" for r in result.value)
    # the properties arrive as stored
    stored = {r["@id"]: r for r in result.value}[external["first"]["@id"]]
    assert stored["specification"] == "select 1" and stored["language"] == "SQL"
    assert stored["elementEnd"] == {"@id": external["element"]["@id"]}
    # none at a project without them
    empty = pim.create_project("no external", "x").value
    commit = pim.create_commit(empty["@id"], payload_change(package("x"))).value
    assert pim.get_external_relationships(empty["@id"], commit["@id"]).value == []


@pytest.mark.conformance("PIM-ER-002", service="ExternalRelationshipService", read=False)
def test_pim_er_002_get_external_relationship_by_id(pim, external):
    result = pim.get_external_relationship_by_id(external["project"]["@id"], external["commit"]["@id"], external["second"]["@id"])
    assert result.status == 200
    assert [r["@id"] for r in result.value] == [external["second"]["@id"]]


@pytest.mark.conformance("PIM-ER-003", service="ExternalRelationshipService", read=False)
def test_pim_er_003_get_external_relationship_by_id_does_not_exist(pim, external):
    pid, cid = external["project"]["@id"], external["commit"]["@id"]
    # unknown id; the id of something that is no ExternalRelationship (the element); and one that was deleted
    assert pim.get_external_relationship_by_id(pid, cid, new_id()).value == []
    assert pim.get_external_relationship_by_id(pid, cid, external["element"]["@id"]).value == []
    deleted = pim.create_commit(pid, [{"identity": {"@id": external["first"]["@id"]}, "payload": None}]).value["@id"]
    assert pim.get_external_relationship_by_id(pid, deleted, external["first"]["@id"]).value == []
    assert [r["@id"] for r in pim.get_external_relationships(pid, deleted).value] == [external["second"]["@id"]]
