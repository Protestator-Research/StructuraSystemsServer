"""A.4 QueryService Conformance Test Cases (PIM-QS-001, 002)."""
import uuid

import pytest

from .conftest import all_record_ids, package, payload_change
from .pim_client import new_id

WHERE = {"@type": "PrimitiveConstraint", "property": "@type", "operator": "=", "value": "Package"}


@pytest.mark.conformance("PIM-QS-001", service="QueryService", read=False)
@pytest.mark.parametrize("with_where", [True, False], ids=["with_where", "without_where"])
def test_pim_qs_001_create_query(pim, mongo, model, with_where):
    pid = model["project"]["@id"]
    before = all_record_ids(mongo)
    scope = [model["a"]["@id"]]
    result = pim.create_query(pid, select=["declaredName"], scope=scope, where=WHERE if with_where else None, order_by=["declaredName"])
    assert result.status == 201, result.response.text
    query = result.value
    assert str(uuid.UUID(query["@id"])) == query["@id"] and query["@id"] not in before
    assert query["owningProject"] == {"@id": pid}
    assert query["select"] == ["declaredName"]
    assert query["scope"] == [{"@id": identity} for identity in scope]
    assert query["orderBy"] == ["declaredName"]
    if with_where:
        assert query["where"]["property"] == "@type" and query["where"]["value"] == "Package" and query["where"]["operator"] == "="
    else:
        assert query.get("where") is None
    # the query is a query of the project
    assert query["@id"] in [q["@id"] for q in pim.get_queries(pid).value]


@pytest.mark.conformance("PIM-QS-002", service="QueryService", read=True)
def test_pim_qs_002_execute_query(pim, model):
    pid, cid = model["project"]["@id"], model["commit"]["@id"]
    # all Data at the commit that satisfy the conditions, whatever the kind of Data
    everything = pim.create_query(pid).value
    result = pim.execute_query_by_id(pid, everything["@id"], cid)
    assert result.status == 200
    assert sorted(d["@id"] for d in result.value) == sorted(model[k]["@id"] for k in ("a", "b", "c", "r1", "r2", "r3", "external"))

    packages = pim.create_query(pid, where=WHERE).value
    assert sorted(d["@id"] for d in pim.execute_query_by_id(pid, packages["@id"], cid).value) == sorted(model[k]["@id"] for k in ("a", "b", "c"))

    # only Data within the scope
    scoped = pim.create_query(pid, scope=[model["b"]["@id"]]).value
    assert [d["@id"] for d in pim.execute_query_by_id(pid, scoped["@id"], cid).value] == [model["b"]["@id"]]

    # the result is the state at the commit
    later = pim.create_commit(pid, payload_change(package("d"))).value["@id"]
    assert len(pim.execute_query_by_id(pid, packages["@id"], later).value) == 4
    assert len(pim.execute_query_by_id(pid, packages["@id"], cid).value) == 3
    unknown_commit = pim.execute_query_by_id(pid, packages["@id"], new_id())
    assert unknown_commit.status == 404 and unknown_commit.communicates_error
