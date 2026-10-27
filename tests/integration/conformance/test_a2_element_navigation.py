"""A.2 ElementNavigationService Conformance Test Cases (PIM-EN-001 ... 005)."""
import pytest

from .conftest import package, payload_change
from .pim_client import new_id


def ids(items):
    return sorted(item["@id"] for item in items)


@pytest.mark.conformance("PIM-EN-001", service="ElementNavigationService", read=False)
def test_pim_en_001_get_elements(pim, model):
    pid, cid = model["project"]["@id"], model["commit"]["@id"]
    result = pim.get_elements(pid, cid)
    assert result.status == 200
    # all Elements at the commit: the packages and the relationships, but not the ExternalData
    expected = [model[key]["@id"] for key in ("a", "b", "c", "r1", "r2", "r3")]
    assert ids(result.value) == sorted(expected)

    # at a later commit that changes one element: the unchanged ones are still there (the cumulative versionedData)
    later = pim.create_commit(pid, payload_change(package("a renamed", model["a"]["@id"])))
    assert later.status == 201
    at_later = pim.get_elements(pid, later.value["@id"])
    assert ids(at_later.value) == sorted(expected)
    assert {e["@id"]: e["declaredName"] for e in at_later.value}[model["a"]["@id"]] == "a renamed"


@pytest.mark.conformance("PIM-EN-002", service="ElementNavigationService", read=False)
def test_pim_en_002_get_element_by_id(pim, model):
    result = pim.get_element_by_id(model["project"]["@id"], model["commit"]["@id"], model["b"]["@id"])
    assert result.status == 200
    assert result.value["@id"] == model["b"]["@id"]
    assert result.value["declaredName"] == "b"


@pytest.mark.conformance("PIM-EN-003", service="ElementNavigationService", read=False)
def test_pim_en_003_get_element_by_id_does_not_exist(pim, model):
    pid, cid = model["project"]["@id"], model["commit"]["@id"]
    unknown = pim.get_element_by_id(pid, cid, new_id())
    assert unknown.status == 404 and unknown.value is None and unknown.communicates_error
    # the ExternalData exists at the commit but is no Element
    not_an_element = pim.get_element_by_id(pid, cid, model["external"]["@id"])
    assert not_an_element.status == 404 and not_an_element.communicates_error
    # an element that was deleted does not exist at the later commit
    deleted = pim.create_commit(pid, [{"identity": {"@id": model["b"]["@id"]}, "payload": None}])
    assert deleted.status == 201
    gone = pim.get_element_by_id(pid, deleted.value["@id"], model["b"]["@id"])
    assert gone.status == 404 and gone.communicates_error


@pytest.mark.conformance("PIM-EN-004", service="ElementNavigationService", read=False)
def test_pim_en_004_get_relationships_by_source(pim, model):
    pid, cid = model["project"]["@id"], model["commit"]["@id"]
    # a is the source of r1 and r2
    assert ids(pim.get_relationships_by_source(pid, cid, model["a"]["@id"]).value) == sorted([model["r1"]["@id"], model["r2"]["@id"]])
    assert ids(pim.get_relationships_by_source(pid, cid, model["c"]["@id"]).value) == [model["r3"]["@id"]]
    # b is the source of nothing
    result = pim.get_relationships_by_source(pid, cid, model["b"]["@id"])
    assert result.status == 200 and result.value == []


@pytest.mark.conformance("PIM-EN-005", service="ElementNavigationService", read=False)
def test_pim_en_005_get_relationships_by_target(pim, model):
    pid, cid = model["project"]["@id"], model["commit"]["@id"]
    # a is the target of r3
    assert ids(pim.get_relationships_by_target(pid, cid, model["a"]["@id"]).value) == [model["r3"]["@id"]]
    assert ids(pim.get_relationships_by_target(pid, cid, model["b"]["@id"]).value) == [model["r1"]["@id"]]
    result = pim.get_relationships_by_target(pid, cid, model["external"]["@id"])
    assert result.status == 404, "the ExternalData is no Element at the commit"
