import uuid
from concurrent.futures import ThreadPoolExecutor

import requests

from conftest import ADMIN_PASSWORD, auth, get_all, login

REQUESTS = 200
WORKERS = 32
TIMEOUT = 120


def test_parallel_requests_keep_the_server_consistent(base_url, server, admin_token, mongo):
    prefix = f"conc_{uuid.uuid4().hex[:8]}"
    headers = auth(admin_token)

    shared = requests.post(f"{base_url}/projects", json={"name": f"{prefix}_shared", "description": "shared"},
                           headers=headers, timeout=TIMEOUT)
    assert shared.status_code == 201
    shared_id = shared.json()["@id"]
    # A new project has no commit; the first commit gives the branches a head.
    first = requests.post(f"{base_url}/projects/{shared_id}/commits", headers=headers, timeout=TIMEOUT,
                          json={"description": "first", "change": [{"payload": {
                              "@type": "TextualRepresentation", "@id": str(uuid.uuid4()), "language": "SysMLv2", "body": "x"}}]})
    assert first.status_code == 201, first.text
    head_commit = first.json()["@id"]

    def commit_body():
        return {
            "@type": "CommitRequest",
            "description": "parallel",
            "change": [{"@type": "DataVersion", "payload": {
                "@type": "TextualRepresentation", "@id": str(uuid.uuid4()), "language": "SysMLv2", "body": "part x;"}}],
        }

    def job(index):
        kind = index % 6
        if kind == 0:
            response = requests.post(f"{base_url}/login", json={"username": "admin", "password": ADMIN_PASSWORD}, timeout=TIMEOUT)
        elif kind == 1:
            response = requests.post(f"{base_url}/projects", json={"name": f"{prefix}_{index}", "description": "parallel"},
                                     headers=headers, timeout=TIMEOUT)
        elif kind == 2:
            response = requests.get(f"{base_url}/projects", headers=headers, timeout=TIMEOUT)
        elif kind == 3:
            response = requests.post(f"{base_url}/projects/{shared_id}/commits", json=commit_body(), headers=headers, timeout=TIMEOUT)
        elif kind == 4:
            response = requests.post(f"{base_url}/projects/{shared_id}/branches",
                                     json={"@type": "Branch", "name": f"b{index}", "head": {"@id": head_commit}},
                                     headers=headers, timeout=TIMEOUT)
        else:
            path = "commits" if index % 12 == 5 else "branches"
            response = requests.get(f"{base_url}/projects/{shared_id}/{path}", headers=headers, timeout=TIMEOUT)
        return kind, index, response

    with ThreadPoolExecutor(WORKERS) as pool:
        results = list(pool.map(job, range(REQUESTS)))

    server_errors = [(kind, index, r.status_code, r.text[:200]) for kind, index, r in results if r.status_code >= 500]
    assert not server_errors, server_errors
    assert server.poll() is None, "server process died"

    expected = {0: 200, 1: 201, 2: 200, 3: 201, 4: 201, 5: 200}
    unexpected = [(kind, index, r.status_code) for kind, index, r in results if r.status_code != expected[kind]]
    assert not unexpected, unexpected

    created_ids = {r.json()["@id"] for kind, _, r in results if kind == 1}
    commit_ids = {r.json()["@id"] for kind, _, r in results if kind == 3}
    branch_ids = {r.json()["@id"] for kind, _, r in results if kind == 4}
    assert len(created_ids) == len([1 for kind, _, _ in results if kind == 1])
    assert len(commit_ids) == len([1 for kind, _, _ in results if kind == 3])

    # The server still answers and all created entities are readable.
    assert requests.get(f"{base_url}/me", headers=headers, timeout=TIMEOUT).status_code == 200
    listed = {p["@id"] for p in get_all(f"{base_url}/projects", headers, timeout=TIMEOUT)}
    assert created_ids <= listed
    for project_id in created_ids:
        assert requests.get(f"{base_url}/projects/{project_id}", headers=headers, timeout=TIMEOUT).status_code == 200

    commits = {c["@id"] for c in get_all(f"{base_url}/projects/{shared_id}/commits", headers, timeout=TIMEOUT)}
    assert commit_ids <= commits
    assert len(commits) == 1 + len(commit_ids)
    for commit_id in list(commit_ids)[:10]:
        assert requests.get(f"{base_url}/projects/{shared_id}/commits/{commit_id}", headers=headers, timeout=TIMEOUT).status_code == 200
    branches = {b["@id"] for b in get_all(f"{base_url}/projects/{shared_id}/branches", headers, timeout=TIMEOUT)}
    assert branch_ids <= branches

    # The database agrees with the in-memory state.
    from conftest import TEST_DB
    database = mongo[TEST_DB]
    assert database["projects"].count_documents({"name": {"$regex": f"^{prefix}_"}}) == len(created_ids) + 1
    assert database["commits"].count_documents({"owningProject.@id": shared_id}) == len(commits)

    # The history of the default branch is linear: every commit but the first has one previous commit, and no two commits share it.
    all_commits = get_all(f"{base_url}/projects/{shared_id}/commits", headers, timeout=TIMEOUT)
    previous = [c["previousCommits"][0]["@id"] for c in all_commits if c["previousCommits"]]
    assert len(previous) == len(all_commits) - 1
    assert len(set(previous)) == len(previous)
    created = [c["created"] for c in all_commits]
    assert len(set(created)) == len(created)
