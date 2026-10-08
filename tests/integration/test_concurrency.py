import uuid
from concurrent.futures import ThreadPoolExecutor

import requests

from conftest import ADMIN_PASSWORD, auth, login

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
    initial_commits = requests.get(f"{base_url}/projects/{shared_id}/commits", headers=headers, timeout=TIMEOUT).json()
    head_commit = initial_commits[-1]["@id"]

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
            # sysmllib reads the branch name from "@type" and the head as a JSON string.
            response = requests.post(f"{base_url}/projects/{shared_id}/branches",
                                     json={"@type": f"b{index}", "name": "x", "head": f'{{"@id":"{head_commit}"}}'},
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

    expected = {0: 200, 1: 201, 2: 200, 3: 200, 4: 200, 5: 200}
    unexpected = [(kind, index, r.status_code) for kind, index, r in results if r.status_code != expected[kind]]
    assert not unexpected, unexpected

    created_ids = {r.json()["@id"] for kind, _, r in results if kind == 1}
    commit_ids = {r.json()["@id"] for kind, _, r in results if kind == 3}
    branch_ids = {r.json()["@id"] for kind, _, r in results if kind == 4}
    assert len(created_ids) == len([1 for kind, _, _ in results if kind == 1])
    assert len(commit_ids) == len([1 for kind, _, _ in results if kind == 3])

    # The server still answers and all created entities are readable.
    assert requests.get(f"{base_url}/me", headers=headers, timeout=TIMEOUT).status_code == 200
    listed = {p["@id"] for p in requests.get(f"{base_url}/projects", headers=headers, timeout=TIMEOUT).json()}
    assert created_ids <= listed
    for project_id in created_ids:
        assert requests.get(f"{base_url}/projects/{project_id}", headers=headers, timeout=TIMEOUT).status_code == 200

    commits = {c["@id"] for c in requests.get(f"{base_url}/projects/{shared_id}/commits", headers=headers, timeout=TIMEOUT).json()}
    assert commit_ids <= commits
    assert len(commits) == 1 + len(commit_ids)
    for commit_id in list(commit_ids)[:10]:
        assert requests.get(f"{base_url}/projects/{shared_id}/commits/{commit_id}", headers=headers, timeout=TIMEOUT).status_code == 200
    branches = {b["@id"] for b in requests.get(f"{base_url}/projects/{shared_id}/branches", headers=headers, timeout=TIMEOUT).json()}
    assert branch_ids <= branches

    # The database agrees with the in-memory state.
    from conftest import TEST_DB
    database = mongo[TEST_DB]
    assert database["projects"].count_documents({"name": {"$regex": f"^{prefix}_"}}) == len(created_ids) + 1
    assert database["commits"].count_documents({"owningProject.@id": shared_id}) == len(commits)
