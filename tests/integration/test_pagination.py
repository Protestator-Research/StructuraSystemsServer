"""Pagination of the collection routes: page[size], page[after], page[before] and the Link header."""
import base64
import uuid
from urllib.parse import parse_qs, urlparse

import pytest
import requests

from conftest import ADMIN_PASSWORD, MONGO_URI, auth, get_all, login, start_server


@pytest.fixture
def headers(admin_token):
    return auth(admin_token)


def new_id():
    return str(uuid.uuid4())


def package(name):
    return {"@type": "Package", "@id": new_id(), "declaredName": name}


def specialization(source, target):
    return {"@type": "Specialization", "@id": new_id(), "source": [{"@id": source}], "target": [{"@id": target}]}


def commit(base_url, headers, project_id, payloads, **params):
    response = requests.post(f"{base_url}/projects/{project_id}/commits", json={"description": "p", "change": [{"payload": p} for p in payloads]},
                             params=params, headers=headers, timeout=10)
    assert response.status_code == 201, response.text
    return response.json()["@id"]


def key_of(item):
    """The identity of a listed item: results and differences may be wrapped."""
    if "baseData" in item:
        return (item["baseData"] or item["compareData"])["identity"]["@id"]
    return item["@id"]


def cursor_of(link_url, name):
    return parse_qs(urlparse(link_url).query)[name][0]


def walk(url, headers, size=1, method="GET", **kwargs):
    """The pages of a collection, following rel="next"."""
    pages = []
    response = requests.request(method, url, headers=headers, params={"page[size]": size, **kwargs.pop("params", {})}, timeout=30, **kwargs)
    while True:
        assert response.status_code == 200, response.text
        pages.append(response.json())
        link = response.links.get("next")
        if link is None:
            return pages, response
        response = requests.request(method, link["url"], headers=headers, timeout=30, **kwargs)


@pytest.fixture
def model(base_url, headers):
    """A project with 5 packages, 3 relationships of the first one, two branches, two tags, queries and twins."""
    response = requests.post(f"{base_url}/projects", json={"name": f"paging_{uuid.uuid4().hex[:8]}", "description": "paging"},
                             headers=headers, timeout=10)
    assert response.status_code == 201, response.text
    pid = response.json()["@id"]
    packages = [package(f"P{index}") for index in range(5)]
    links = [specialization(packages[0]["@id"], packages[index]["@id"]) for index in range(1, 4)]
    first = commit(base_url, headers, pid, packages + links)
    second = commit(base_url, headers, pid, [package("Q0"), package("Q1"), package("Q2")])
    for index in range(2):
        assert requests.post(f"{base_url}/projects/{pid}/branches", json={"name": f"b{index}", "head": {"@id": first}}, headers=headers,
                             timeout=10).status_code == 201
        assert requests.post(f"{base_url}/projects/{pid}/tags", json={"name": f"t{index}", "taggedCommit": {"@id": first}}, headers=headers,
                             timeout=10).status_code == 201
        assert requests.post(f"{base_url}/projects/{pid}/queries", json={"@type": "Query", "name": f"q{index}"}, headers=headers,
                             timeout=10).status_code == 201
        assert requests.post(f"{base_url}/projects/{pid}/twins", json={"name": f"twin{index}", "referencedCommit": {"@id": first}},
                             headers=headers, timeout=10).status_code == 201
    return {"pid": pid, "first": first, "second": second, "packages": packages, "links": links}


def routes(base_url, model):
    pid, first, second = model["pid"], model["first"], model["second"]
    root = model["packages"][0]["@id"]
    return {
        "projects": (f"{base_url}/projects", "GET", {}),
        "commits": (f"{base_url}/projects/{pid}/commits", "GET", {}),
        "branches": (f"{base_url}/projects/{pid}/branches", "GET", {}),
        "tags": (f"{base_url}/projects/{pid}/tags", "GET", {}),
        "elements": (f"{base_url}/projects/{pid}/commits/{second}/elements", "GET", {}),
        "roots": (f"{base_url}/projects/{pid}/commits/{second}/roots", "GET", {}),
        "root_alias": (f"{base_url}/projects/{pid}/commits/{second}/root", "GET", {}),
        "relationships": (f"{base_url}/projects/{pid}/commits/{first}/elements/{root}/relationships", "GET", {}),
        "changes": (f"{base_url}/projects/{pid}/commits/{first}/changes", "GET", {}),
        "diff": (f"{base_url}/projects/{pid}/commits/{second}/diff", "GET", {"baseCommitId": first}),
        "queries": (f"{base_url}/projects/{pid}/queries", "GET", {}),
        "twins": (f"{base_url}/projects/{pid}/twins", "GET", {}),
    }


ROUTE_NAMES = ["projects", "commits", "branches", "tags", "elements", "roots", "root_alias", "relationships", "changes", "diff",
               "queries", "twins"]


@pytest.mark.parametrize("name", ROUTE_NAMES)
def test_every_collection_route_pages_with_size_one(base_url, headers, model, name):
    url, method, params = routes(base_url, model)[name]
    everything = requests.request(method, url, headers=headers, params={**params, "page[size]": 1000}, timeout=30)
    assert everything.status_code == 200, everything.text
    expected = [key_of(item) for item in everything.json()]
    assert "Link" not in everything.headers or "next" not in everything.links or len(expected) >= 1000

    pages, _ = walk(url, headers, 1, method, params=params)
    walked = [key_of(item) for page in pages for item in page]
    assert walked == expected
    assert len(set(walked)) == len(walked)
    if name not in ("projects",):
        assert len(expected) >= 2, name
        assert all(len(page) == 1 for page in pages)
    # the default order is the order of the full list
    assert [key_of(item) for item in requests.request(method, url, headers=headers, params=params, timeout=30).json()][:len(expected)] == \
        expected[:100]


def test_query_results_are_paged(base_url, headers, model):
    pid, first = model["pid"], model["first"]
    stored = requests.post(f"{base_url}/projects/{pid}/queries", json={"@type": "Query", "where": {
        "@type": "PrimitiveConstraint", "property": "@type", "operator": "=", "value": "Package"}}, headers=headers, timeout=10).json()
    params = {"commitId": first}
    full = requests.get(f"{base_url}/projects/{pid}/queries/{stored['@id']}/results", params={**params, "page[size]": 1000}, headers=headers, timeout=10).json()
    assert len(full) == 5

    for url, method, extra in (
        (f"{base_url}/projects/{pid}/queries/{stored['@id']}/results", "GET", {}),
        (f"{base_url}/projects/{pid}/query-results", "GET", {"json": {"@type": "Query", "where": stored["where"]}}),
        (f"{base_url}/projects/{pid}/query-results", "POST", {"json": {"@type": "Query", "where": stored["where"]}}),
    ):
        pages, _ = walk(url, headers, 2, method, params=params, **extra)
        assert [len(page) for page in pages] == [2, 2, 1]
        assert [row["@id"] for page in pages for row in page] == [row["@id"] for row in full]


def test_default_page_size_is_100(base_url, headers):
    response = requests.post(f"{base_url}/projects", json={"name": f"big_{uuid.uuid4().hex[:8]}", "description": "x"}, headers=headers, timeout=10)
    pid = response.json()["@id"]
    for index in range(101):
        commit(base_url, headers, pid, [package(f"E{index}")])
    first_page = requests.get(f"{base_url}/projects/{pid}/commits", headers=headers, timeout=30)
    assert len(first_page.json()) == 100
    assert "next" in first_page.links
    assert "prev" not in first_page.links
    second_page = requests.get(first_page.links["next"]["url"], headers=headers, timeout=30)
    assert len(second_page.json()) == 1
    assert "next" not in second_page.links
    assert len(get_all(f"{base_url}/projects/{pid}/commits", headers)) == 101
    # the maximum is accepted
    assert len(requests.get(f"{base_url}/projects/{pid}/commits", params={"page[size]": 1000}, headers=headers, timeout=30).json()) == 101


def test_no_link_header_without_further_pages(base_url, headers, model):
    response = requests.get(f"{base_url}/projects/{model['pid']}/branches", headers=headers, timeout=10)
    assert response.status_code == 200
    assert "Link" not in response.headers
    response = requests.get(f"{base_url}/projects/{model['pid']}/branches", params={"page[size]": 3}, headers=headers, timeout=10)
    assert len(response.json()) == 3
    assert "Link" not in response.headers


def test_prev_goes_back(base_url, headers, model):
    url = f"{base_url}/projects/{model['pid']}/commits/{model['first']}/elements"
    everything = [e["@id"] for e in requests.get(url, params={"page[size]": 1000}, headers=headers, timeout=10).json()]
    assert len(everything) == 8

    page1 = requests.get(url, params={"page[size]": 3}, headers=headers, timeout=10)
    assert [e["@id"] for e in page1.json()] == everything[:3]
    assert "prev" not in page1.links
    page2 = requests.get(page1.links["next"]["url"], headers=headers, timeout=10)
    assert [e["@id"] for e in page2.json()] == everything[3:6]
    page3 = requests.get(page2.links["next"]["url"], headers=headers, timeout=10)
    assert [e["@id"] for e in page3.json()] == everything[6:]
    assert "next" not in page3.links

    back2 = requests.get(page3.links["prev"]["url"], headers=headers, timeout=10)
    assert [e["@id"] for e in back2.json()] == everything[3:6]
    assert back2.links["next"]["url"] == page2.links["next"]["url"]
    back1 = requests.get(back2.links["prev"]["url"], headers=headers, timeout=10)
    assert [e["@id"] for e in back1.json()] == everything[:3]
    assert "prev" not in back1.links

    # a page before an item that is closer to the start than the page size is shorter
    short = requests.get(url, params={"page[size]": 5, "page[before]": cursor_of(page2.links["prev"]["url"], "page[before]")},
                         headers=headers, timeout=10)
    assert [e["@id"] for e in short.json()] == everything[:3]


def test_link_urls_are_absolute_and_keep_the_other_parameters(base_url, headers, model):
    url = f"{base_url}/projects/{model['pid']}/commits/{model['second']}/diff"
    response = requests.get(url, params={"baseCommitId": model["first"], "changeTypes": "CREATED", "page[size]": 1}, headers=headers, timeout=10)
    assert response.status_code == 200
    assert len(response.json()) == 1
    link = response.links["next"]
    assert link["url"].startswith(url + "?")
    assert "changeTypes=CREATED" in link["url"]
    query = parse_qs(urlparse(link["url"]).query)
    assert query["page[size]"] == ["1"]
    assert query["baseCommitId"] == [model["first"]]
    assert len(query["page[after]"]) == 1 and "page[before]" not in query
    assert "Link" in response.headers and 'rel="next"' in response.headers["Link"]
    followed = requests.get(link["url"], headers=headers, timeout=10)
    assert followed.status_code == 200
    assert followed.json()[0]["compareData"]["identity"]["@id"] != response.json()[0]["compareData"]["identity"]["@id"]


def test_forwarded_headers_of_a_trusted_proxy_are_used(base_url, headers, model):
    # the test server trusts 127.0.0.1 (the default), where these requests come from
    response = requests.get(f"{base_url}/projects/{model['pid']}/commits", params={"page[size]": 1},
                            headers={**headers, "X-Forwarded-Host": "api.example.com", "X-Forwarded-Proto": "https"}, timeout=10)
    assert response.links["next"]["url"].startswith(f"https://api.example.com/projects/{model['pid']}/commits?")
    # a forwarded host that is no host name is not used
    bad = requests.get(f"{base_url}/projects/{model['pid']}/commits", params={"page[size]": 1},
                       headers={**headers, "X-Forwarded-Host": "evil.example/x y"}, timeout=10)
    assert bad.links["next"]["url"].startswith(base_url + "/")


def test_forwarded_headers_are_ignored_without_a_trusted_proxy(mongo):
    db_name, port = "structura_systems_test_paging", 18093
    base = f"http://127.0.0.1:{port}"
    try:
        with start_server(db_name, port, custom_config={"trusted_proxies": []}):
            token_headers = auth(login(base, "admin", ADMIN_PASSWORD))
            pid = requests.post(f"{base}/projects", json={"name": "x", "description": "x"}, headers=token_headers, timeout=10).json()["@id"]
            commit(base, token_headers, pid, [package("A")])
            commit(base, token_headers, pid, [package("B")])
            response = requests.get(f"{base}/projects/{pid}/commits", params={"page[size]": 1},
                                    headers={**token_headers, "X-Forwarded-Host": "evil.example", "X-Forwarded-Proto": "https"}, timeout=10)
            link = response.links["next"]["url"]
            assert link.startswith(base + "/")
            assert "evil.example" not in link
    finally:
        from pymongo import MongoClient
        MongoClient(MONGO_URI).drop_database(db_name)


@pytest.mark.parametrize("size", ["0", "-1", "abc", "", "1.5", "1001", "99999999999", " 5", "5x", "0x10"])
def test_invalid_page_size_returns_400(base_url, headers, model, size):
    response = requests.get(f"{base_url}/projects/{model['pid']}/commits", params={"page[size]": size}, headers=headers, timeout=10)
    assert response.status_code == 400, response.text
    assert "error" in response.json()


def test_invalid_cursors_return_400(base_url, headers, model):
    url = f"{base_url}/projects/{model['pid']}/commits"
    first = requests.get(url, params={"page[size]": 1}, headers=headers, timeout=10)
    cursor = cursor_of(first.links["next"]["url"], "page[after]")
    assert requests.get(url, params={"page[after]": cursor}, headers=headers, timeout=10).status_code == 200
    # a cursor is for one collection: the id belongs to no item of another one
    assert requests.get(f"{base_url}/projects/{model['pid']}/branches", params={"page[after]": cursor}, headers=headers, timeout=10).status_code == 400
    # a cursor of the right form for an unknown item
    forged = base64.urlsafe_b64encode(b"UNKNOWN").decode().rstrip("=")
    for name in ("page[after]", "page[before]"):
        for value in ("", "!!!", "%%%", "a b", forged, cursor + "x", "A" * 5000):
            response = requests.get(url, params={name: value}, headers=headers, timeout=10)
            assert response.status_code == 400, f"{name}={value[:20]!r}: {response.status_code}"
    both = requests.get(url, params={"page[after]": cursor, "page[before]": cursor}, headers=headers, timeout=10)
    assert both.status_code == 400


def test_errors_of_the_route_come_before_paging(base_url, headers, model):
    response = requests.get(f"{base_url}/projects/{model['pid']}/commits/{uuid.uuid4()}/elements", params={"page[size]": 0}, headers=headers, timeout=10)
    assert response.status_code == 404
    response = requests.get(f"{base_url}/projects/{uuid.uuid4()}/commits", params={"page[size]": 1}, headers=headers, timeout=10)
    assert response.status_code == 404


def test_pages_are_stable_while_the_list_grows(base_url, headers, model):
    pid = model["pid"]
    url = f"{base_url}/projects/{pid}/commits"
    page1 = requests.get(url, params={"page[size]": 1}, headers=headers, timeout=10)
    commit(base_url, headers, pid, [package("late")])
    page2 = requests.get(page1.links["next"]["url"], headers=headers, timeout=10)
    assert page2.status_code == 200
    assert page2.json()[0]["@id"] == model["second"]
