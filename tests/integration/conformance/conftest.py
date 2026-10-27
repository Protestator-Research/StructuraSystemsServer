"""Fixtures and the conformance report of the Annex A test cases."""
import json
import os
import uuid
from collections import defaultdict
from pathlib import Path

import pytest

from conftest import auth
from .pim_client import PimClient

_RESULTS = defaultdict(dict)


def pytest_configure(config):
    config.addinivalue_line(
        "markers",
        'conformance(id, service, read=False): Annex A test case id (for example "PIM-PCB-010"), the service whose conformance it belongs to, '
        "and whether it belongs to the Read conformance of that service as well.")


@pytest.hookimpl(hookwrapper=True)
def pytest_runtest_makereport(item, call):
    outcome = yield
    report = outcome.get_result()
    marker = item.get_closest_marker("conformance")
    if marker is None or report.when != "call":
        return
    case = marker.args[0]
    service = marker.kwargs.get("service", "")
    entry = _RESULTS[service].setdefault(case, {"read": marker.kwargs.get("read", False), "passed": 0, "failed": 0})
    entry["passed" if report.passed else "failed"] += 1


def pytest_terminal_summary(terminalreporter):
    if not _RESULTS:
        return
    lines = ["Conformance (Annex A test cases that ran)"]
    for service in sorted(_RESULTS):
        cases = _RESULTS[service]
        failed = sorted(case for case, entry in cases.items() if entry["failed"])
        lines.append(f"  {service}: {len(cases) - len(failed)}/{len(cases)} test cases passed" + (f", failed: {', '.join(failed)}" if failed else ""))
    terminalreporter.write_sep("-", "conformance")
    for line in lines:
        terminalreporter.write_line(line)
    path = os.environ.get("CONFORMANCE_REPORT")
    if path:
        Path(path).write_text(json.dumps(_RESULTS, indent=2, sort_keys=True))


def all_record_ids(mongo):
    """Record.allInstances().id: the ids of all projects, branches, commits, data versions, tags and queries."""
    from conftest import TEST_DB
    database = mongo[TEST_DB]
    ids = set()
    for collection in ("projects", "branches", "commits", "tags", "queries"):
        ids.update(document["_id"] for document in database[collection].find({}, {"_id": 1}))
    for commit in database["commits"].find({}, {"change": 1}):
        ids.update(version["@id"] for version in commit.get("change", []) if "@id" in version)
    return ids


def package(name, element_id=None):
    return {"@type": "Package", "@id": element_id or str(uuid.uuid4()), "declaredName": name}


def relationship(source, target):
    return {"@type": "Specialization", "@id": str(uuid.uuid4()), "source": [{"@id": source}], "target": [{"@id": target}]}


def payload_change(*payloads):
    return [{"payload": payload} for payload in payloads]


@pytest.fixture
def pim(base_url, admin_token):
    return PimClient(base_url, auth(admin_token))


@pytest.fixture
def project(pim):
    result = pim.create_project(f"conformance_{uuid.uuid4().hex[:8]}", "conformance")
    assert result.status == 201, result.response.text
    return result.value


@pytest.fixture
def model(pim, project):
    """Three packages and three relationships (a is the source of r1 and r2 and the target of r3), one ExternalData."""
    a, b, c = package("a"), package("b"), package("c")
    r1, r2, r3 = relationship(a["@id"], b["@id"]), relationship(a["@id"], c["@id"]), relationship(c["@id"], a["@id"])
    external = {"@type": "ExternalData", "@id": str(uuid.uuid4()), "resourceIdentifier": "https://example.com/data"}
    commit = pim.create_commit(project["@id"], payload_change(a, b, c, r1, r2, r3, external))
    assert commit.status == 201, commit.response.text
    return {"project": project, "commit": commit.value, "a": a, "b": b, "c": c, "r1": r1, "r2": r2, "r3": r3, "external": external}


@pytest.fixture
def other_model(pim):
    """A second project with a commit, for the cases where something belongs to another project."""
    project = pim.create_project(f"conformance_other_{uuid.uuid4().hex[:8]}", "conformance").value
    commit = pim.create_commit(project["@id"], payload_change(package("foreign")))
    assert commit.status == 201, commit.response.text
    return {"project": project, "commit": commit.value}
