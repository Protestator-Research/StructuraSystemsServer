import time
import uuid

import pytest
import requests
from argon2 import PasswordHasher
from pymongo import MongoClient

from conftest import ADMIN_PASSWORD, MONGO_URI, TEST_DB, auth, login, start_server

MIG_DB = "structura_systems_migtest"
MIG_PORT = 18089
assert MIG_DB != "structura_systems"

COLLECTIONS = ["projects", "branches", "commits", "data_versions", "data_elements",
               "tags", "users", "digital_twins", "versions", "schema"]


@pytest.fixture(scope="module")
def mig_client():
    client = MongoClient(MONGO_URI, serverSelectionTimeoutMS=5000)
    client.drop_database(MIG_DB)
    yield client
    client.drop_database(MIG_DB)
    client.close()


def seed_project(db, name, description, **extra):
    project_id = str(uuid.uuid4())
    branch_id = str(uuid.uuid4())
    commit_id = str(uuid.uuid4())
    # Shape copied from documents written by the version 1 server.
    db.projects.insert_one({
        "_id": project_id, "@type": "Project",
        "defaultBranch": {"@id": branch_id},
        "description": description, "name": name, **extra,
    })
    db.branches.insert_one({
        "_id": branch_id, "@type": "Branch", "_project_id": project_id,
        "created": "2026-07-29T09:01:35.128395",
        "deleted": "1677-09-21T00:12:44.-854775",
        "head": {"@id": commit_id}, "name": "Main",
        "referencedCommit": {"@id": commit_id},
    })
    db.commits.insert_one({
        "_id": commit_id, "@type": "Commit", "alias": [""], "change": [],
        "description": "Initial Commit", "owningProject": {"@id": project_id},
        "previousCommit": [], "timestamp": "2026-07-29T09:01:35.143292",
    })
    return project_id


def seed_v1(client):
    client.drop_database(MIG_DB)
    db = client[MIG_DB]
    for name in COLLECTIONS:
        if name != "schema":
            db.create_collection(name)
    db.versions.insert_one({"_id": "none", "build": "none", "version": "None"})
    ids = {
        "library": seed_project(db, "Lib.sysml", "Preloaded Project"),
        "orphan": seed_project(db, "Orphan", "Some user project"),
        "alice": seed_project(db, "AliceProject", "Alice's", owner="alice"),
    }
    # Legacy user documents: invalid role and no group.
    db.users.insert_one({"_id": "badrole", "securityString": "x", "role": -1})
    db.users.insert_one({
        "_id": "admin", "group": "", "role": 100,
        "securityString": PasswordHasher().hash("admin"),
    })
    return ids


def snapshot(db):
    return {name: sorted(db[name].find({}), key=lambda d: str(d["_id"]))
            for name in db.list_collection_names()}


def test_migration_from_v1(mig_client):
    ids = seed_v1(mig_client)
    db = mig_client[MIG_DB]

    with start_server(MIG_DB, MIG_PORT, drop_db=False) as (proc, output):
        pass
    log = output.read_text(errors="replace")
    assert "[migration]" in log

    assert db.projects.find_one({"_id": ids["library"]})["isLibrary"] is True
    assert "isLibrary" not in db.projects.find_one({"_id": ids["orphan"]})
    alice = db.projects.find_one({"_id": ids["alice"]})
    assert "isLibrary" not in alice and alice["owner"] == "alice"
    assert "Orphan" in log and ids["orphan"] in log

    badrole = db.users.find_one({"_id": "badrole"})
    assert badrole["role"] == 0
    assert badrole["group"] == ""
    assert badrole["mustChangePassword"] is False

    admin = db.users.find_one({"_id": "admin"})
    assert admin["mustChangePassword"] is True
    assert admin["role"] == 100

    # the migration to 3 ran as well: the branch got its project, a commit without data versions is untouched
    branch = db.branches.find_one({"_project_id": ids["alice"]})
    assert branch["owningProject"] == {"@id": ids["alice"]}
    assert db.commits.find_one({"owningProject.@id": ids["alice"]})["change"] == []

    assert db.schema.find_one({"_id": "schema"})["version"] == 3


def test_migration_is_idempotent(mig_client):
    db = mig_client[MIG_DB]
    assert db.schema.find_one({"_id": "schema"})["version"] == 3
    before = snapshot(db)
    with start_server(MIG_DB, MIG_PORT, drop_db=False):
        pass
    assert snapshot(db) == before


def test_newer_schema_refuses_start(mig_client):
    db = mig_client[MIG_DB]
    before = snapshot(db)
    db.schema.update_one({"_id": "schema"}, {"$set": {"version": 99}})
    try:
        with start_server(MIG_DB, MIG_PORT, drop_db=False, expect_ready=False) as (proc, _):
            assert proc.wait(timeout=60) != 0
    finally:
        db.schema.update_one({"_id": "schema"}, {"$set": {"version": 3}})
    assert snapshot(db) == before


def test_fresh_database_has_current_schema(server, mongo):
    db = mongo[TEST_DB]
    assert db.schema.find_one({"_id": "schema"})["version"] == 3
    projects = list(db.projects.find({"description": "Preloaded Project"}))
    assert projects
    assert all(p.get("isLibrary") is True for p in projects)


def package_payload(element_id, name):
    return {"@id": element_id, "@type": "Package", "declaredName": name}


def seed_v2(client):
    """A schema 2 database as the 2610.2 server wrote it: DataVersions without @id, branches without owningProject."""
    client.drop_database(MIG_DB)
    db = client[MIG_DB]
    for name in COLLECTIONS:
        if name != "schema":
            db.create_collection(name)
    db.versions.insert_one({"_id": "none", "build": "none", "version": "None"})
    db.schema.insert_one({"_id": "schema", "version": 2})

    project_id, branch_id = str(uuid.uuid4()), str(uuid.uuid4())
    first_commit, second_commit = str(uuid.uuid4()), str(uuid.uuid4())
    element_a, element_b, element_c = (str(uuid.uuid4()) for _ in range(3))
    version_c = str(uuid.uuid4())
    db.projects.insert_one({
        "_id": project_id, "@type": "Project", "name": "V2Project", "description": "x", "owner": "nobody",
        "defaultBranch": {"@id": branch_id},
    })
    db.branches.insert_one({
        "_id": branch_id, "@type": "Branch", "_project_id": project_id, "name": "main",
        "created": "2026-07-29T09:01:35.128395Z", "owningProject": None,
        "head": {"@id": second_commit}, "referencedCommit": {"@id": second_commit},
    })
    db.commits.insert_one({
        "_id": first_commit, "@type": "Commit", "owningProject": {"@id": project_id}, "previousCommit": [],
        "created": "2026-07-29T09:01:35.143292Z", "description": "first",
        "change": [
            # written by the request path: no @id, no commit
            {"@type": "DataVersion", "identity": {"@id": element_a, "@type": "DataIdentity"},
             "payload": package_payload(element_a, "A")},
            # written by the first library versions: the @id is the id of the payload
            {"@id": element_b, "@type": "DataVersion", "payload": package_payload(element_b, "B")},
        ],
    })
    db.commits.insert_one({
        "_id": second_commit, "@type": "Commit", "owningProject": {"@id": project_id},
        "previousCommit": [{"@id": first_commit}], "created": "2026-07-29T09:02:35.143292Z", "description": "second",
        "change": [
            # complete already
            {"@id": version_c, "@type": "DataVersion", "identity": {"@id": element_c, "@type": "DataIdentity"},
             "payload": package_payload(element_c, "C"), "commit": {"@id": second_commit}},
        ],
    })
    return {"project": project_id, "branch": branch_id, "first": first_commit, "second": second_commit,
            "a": element_a, "b": element_b, "c": element_c, "version_c": version_c}


def test_migration_from_v2_completes_data_versions_and_branches(mig_client):
    ids = seed_v2(mig_client)
    db = mig_client[MIG_DB]

    with start_server(MIG_DB, MIG_PORT, drop_db=False) as (proc, output):
        log = output.read_text(errors="replace")
        assert "2 -> 3" in log
        assert db.schema.find_one({"_id": "schema"})["version"] == 3

        assert db.branches.find_one({"_id": ids["branch"]})["owningProject"] == {"@id": ids["project"]}

        first = db.commits.find_one({"_id": ids["first"]})
        version_ids = set()
        for entry, element in zip(first["change"], (ids["a"], ids["b"])):
            assert str(uuid.UUID(entry["@id"])) == entry["@id"]
            assert entry["@id"] != element
            assert entry["identity"] == {"@id": element, "@type": "DataIdentity"}
            assert entry["commit"] == {"@id": ids["first"]}
            assert entry["payload"]["@id"] == element
            version_ids.add(entry["@id"])
        assert len(version_ids) == 2

        # what was complete stays as it is
        second = db.commits.find_one({"_id": ids["second"]})
        assert second["change"][0]["@id"] == ids["version_c"]
        assert second["change"][0]["commit"] == {"@id": ids["second"]}

        # the server serves the migrated data
        base = f"http://127.0.0.1:{MIG_PORT}"
        token = login(base, "admin", ADMIN_PASSWORD)
        changes = requests.get(f"{base}/projects/{ids['project']}/commits/{ids['first']}/changes",
                               headers=auth(token), timeout=10)
        assert changes.status_code == 200, changes.text
        assert {entry["@id"] for entry in changes.json()} == version_ids

        branch = requests.get(f"{base}/projects/{ids['project']}/branches/{ids['branch']}", headers=auth(token), timeout=10).json()
        assert branch["owningProject"] == {"@id": ids["project"]}
        assert branch["head"] == {"@id": ids["second"]}

    # a second start changes nothing
    before = snapshot(db)
    with start_server(MIG_DB, MIG_PORT, drop_db=False):
        pass
    assert snapshot(db) == before


def test_nonempty_db_without_versions_is_not_dropped(mig_client):
    mig_client.drop_database(MIG_DB)
    db = mig_client[MIG_DB]
    db.projects.insert_one({"_id": "keepme", "@type": "Project", "name": "Keep",
                            "description": "x", "defaultBranch": {"@id": "b"}})
    with start_server(MIG_DB, MIG_PORT, drop_db=False, expect_ready=False) as (proc, output):
        deadline = time.time() + 60
        while proc.poll() is None and time.time() < deadline:
            time.sleep(0.2)
        assert proc.poll() is not None, "server must exit"
        assert proc.returncode != 0
    assert "refusing" in output.read_text(errors="replace").lower()
    assert db.projects.find_one({"_id": "keepme"}) is not None
    assert "versions" not in db.list_collection_names()
