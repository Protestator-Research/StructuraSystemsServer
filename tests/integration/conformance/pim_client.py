"""Adapter from the PIM operations of the Systems Modeling API and Services to the REST/HTTP PSM (Table 10).

One method per PIM operation. Every method returns a PimResult, so the test cases read like the pre- and postconditions of
Annex A. A second PSM (for example OSLC) would only need another adapter with the same methods.
"""
import uuid
from dataclasses import dataclass
from typing import Any, Optional

import requests

TIMEOUT = 30
MISSING = object()


@dataclass
class PimResult:
    response: requests.Response

    @property
    def status(self) -> int:
        return self.response.status_code

    @property
    def ok(self) -> bool:
        return self.status in (200, 201)

    @property
    def value(self) -> Any:
        """The result of the operation; None if the operation did not succeed (the result is empty)."""
        return self.response.json() if self.ok else None

    @property
    def communicates_error(self) -> bool:
        """The answer has no result and says why, as JSON: {"error": ...}."""
        if self.ok or not self.response.headers.get("Content-Type", "").startswith("application/json"):
            return False
        body = self.response.json()
        return isinstance(body, dict) and bool(body.get("error"))


def new_id() -> str:
    return str(uuid.uuid4())


class PimClient:
    def __init__(self, base_url, headers):
        self.base = base_url
        self.headers = headers

    def _call(self, method, path, json=None, params=None) -> PimResult:
        return PimResult(requests.request(method, f"{self.base}{path}", json=json, params=params, headers=self.headers, timeout=TIMEOUT))

    # --- ProjectService ---------------------------------------------------------------------------------------------

    def create_project(self, name=MISSING, description=MISSING):
        body = {"@type": "Project"}
        if name is not MISSING:
            body["name"] = name
        if description is not MISSING:
            body["description"] = description
        return self._call("POST", "/projects", body)

    def get_projects(self):
        """All projects (the pages of the collection are followed)."""
        items, result = [], self._call("GET", "/projects")
        while True:
            if not result.ok:
                return result, None
            items.extend(result.value)
            next_url = result.response.links.get("next", {}).get("url")
            if not next_url:
                return result, items
            result = PimResult(requests.get(next_url, headers=self.headers, timeout=TIMEOUT))

    def get_project_by_id(self, project_id):
        return self._call("GET", f"/projects/{project_id}")

    # --- ElementNavigationService -----------------------------------------------------------------------------------

    def get_elements(self, project_id, commit_id):
        return self._call("GET", f"/projects/{project_id}/commits/{commit_id}/elements", params={"page[size]": 1000})

    def get_element_by_id(self, project_id, commit_id, element_id):
        return self._call("GET", f"/projects/{project_id}/commits/{commit_id}/elements/{element_id}")

    def get_relationships_by_source(self, project_id, commit_id, element_id):
        return self._call("GET", f"/projects/{project_id}/commits/{commit_id}/elements/{element_id}/relationships",
                          params={"direction": "out", "page[size]": 1000})

    def get_relationships_by_target(self, project_id, commit_id, element_id):
        return self._call("GET", f"/projects/{project_id}/commits/{commit_id}/elements/{element_id}/relationships",
                          params={"direction": "in", "page[size]": 1000})

    # --- ProjectDataVersioningService -------------------------------------------------------------------------------

    def create_branch(self, project_id, name=MISSING, head=MISSING):
        body = {"@type": "Branch"}
        if name is not MISSING:
            body["name"] = name
        if head is not MISSING:
            body["head"] = {"@id": head}
        return self._call("POST", f"/projects/{project_id}/branches", body)

    def get_branches(self, project_id):
        return self._call("GET", f"/projects/{project_id}/branches", params={"page[size]": 1000})

    def get_branch_by_id(self, project_id, branch_id):
        return self._call("GET", f"/projects/{project_id}/branches/{branch_id}")

    def delete_branch(self, project_id, branch_id):
        return self._call("DELETE", f"/projects/{project_id}/branches/{branch_id}")

    def get_default_branch(self, project_id):
        project = self.get_project_by_id(project_id)
        if not project.ok:
            return project
        return self.get_branch_by_id(project_id, project.value["defaultBranch"]["@id"])

    def set_default_branch(self, project_id, branch_id=MISSING):
        body = {"@type": "Project"}
        if branch_id is not MISSING:
            body["defaultBranch"] = {"@id": branch_id} if branch_id else {}
        return self._call("PUT", f"/projects/{project_id}", body)

    def create_commit(self, project_id, change, branch_id=None, previous_commits=(), description="conformance"):
        body = {"@type": "Commit", "description": description, "change": change}
        if previous_commits:
            body["previousCommits"] = [{"@id": commit_id} for commit_id in previous_commits]
        return self._call("POST", f"/projects/{project_id}/commits", body, params={"branchId": branch_id} if branch_id else None)

    def get_commit_by_id(self, project_id, commit_id):
        return self._call("GET", f"/projects/{project_id}/commits/{commit_id}")

    def get_head(self, project_id, branch_id):
        """The head commit of the branch: the branch says which commit, the commit is then read by its id."""
        branch = self.get_branch_by_id(project_id, branch_id)
        if not branch.ok:
            return branch
        head = branch.value.get("head")
        if not head:
            return branch
        return self.get_commit_by_id(project_id, head["@id"])

    # --- QueryService -----------------------------------------------------------------------------------------------

    def create_query(self, project_id, select=(), scope=(), where=None, order_by=()):
        body = {"@type": "Query", "select": list(select), "scope": [{"@id": identity} for identity in scope], "orderBy": list(order_by)}
        if where is not None:
            body["where"] = where
        return self._call("POST", f"/projects/{project_id}/queries", body)

    def get_queries(self, project_id):
        return self._call("GET", f"/projects/{project_id}/queries", params={"page[size]": 1000})

    def execute_query_by_id(self, project_id, query_id, commit_id=None):
        return self._call("GET", f"/projects/{project_id}/queries/{query_id}/results",
                          params={"commitId": commit_id, "page[size]": 1000} if commit_id else {"page[size]": 1000})

    # --- ExternalRelationshipService and ProjectUsageService: Table 10 maps them to queries on the @type -----------------

    def _run_type_query(self, project_id, commit_id, type_name, extra=None):
        primitive = {"@type": "PrimitiveConstraint", "property": "@type", "operator": "=", "value": type_name}
        where = primitive if extra is None else {"@type": "CompositeConstraint", "operator": "and", "constraint": [primitive, extra]}
        created = self.create_query(project_id, where=where)
        if not created.ok:
            return created
        return self.execute_query_by_id(project_id, created.value["@id"], commit_id)

    def get_external_relationships(self, project_id, commit_id):
        return self._run_type_query(project_id, commit_id, "ExternalRelationship")

    def get_external_relationship_by_id(self, project_id, commit_id, external_relationship_id):
        """An empty result (200, no entry) is how the PSM communicates 'does not exist' for a query."""
        extra = {"@type": "PrimitiveConstraint", "property": "@id", "operator": "=", "value": external_relationship_id}
        return self._run_type_query(project_id, commit_id, "ExternalRelationship", extra)

    def get_project_usages(self, project_id, commit_id):
        return self._run_type_query(project_id, commit_id, "ProjectUsage")
