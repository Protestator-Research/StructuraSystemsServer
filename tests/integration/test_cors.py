import pytest
import requests

from conftest import start_server

CORS_DB = "structura_systems_corstest"
CORS_PORT = 18095
ALLOWED = "https://app.example.com"
LEGACY = "https://legacy.example.com"


@pytest.fixture(scope="module")
def cors_url():
    config = {"allowed_origins": [ALLOWED], "allowed_origin": LEGACY}
    with start_server(CORS_DB, CORS_PORT, custom_config=config):
        yield f"http://127.0.0.1:{CORS_PORT}"


def vary_values(response):
    return [value.strip() for value in response.headers.get("Vary", "").split(",")]


@pytest.mark.parametrize("origin", [ALLOWED, LEGACY])
def test_allowed_origin_is_reflected(cors_url, origin):
    response = requests.post(f"{cors_url}/login", json={}, headers={"Origin": origin}, timeout=10)
    assert response.headers["Access-Control-Allow-Origin"] == origin
    assert response.headers["Access-Control-Allow-Credentials"] == "true"
    assert response.headers["Access-Control-Expose-Headers"] == "Link"
    assert "Origin" in vary_values(response)


@pytest.mark.parametrize("origin", [ALLOWED, LEGACY])
def test_preflight_for_allowed_origin(cors_url, origin):
    response = requests.options(f"{cors_url}/projects", headers={
        "Origin": origin,
        "Access-Control-Request-Method": "GET",
    }, timeout=10)
    assert response.status_code == 204
    assert response.headers["Access-Control-Allow-Origin"] == origin
    assert response.headers["Access-Control-Allow-Credentials"] == "true"
    assert "GET" in response.headers["Access-Control-Allow-Methods"]
    assert "Authorization" in response.headers["Access-Control-Allow-Headers"]
    assert "Origin" in vary_values(response)


@pytest.mark.parametrize("origin", [
    "https://evil.example.com",
    "https://app.example.com.evil.com",
    "http://app.example.com",
    "https://APP.example.com",
    "https://app.example.com/",
    "null",
    "*",
])
def test_unknown_origin_gets_no_cors_headers(cors_url, origin):
    for response in (
        requests.post(f"{cors_url}/login", json={}, headers={"Origin": origin}, timeout=10),
        requests.options(f"{cors_url}/projects", headers={"Origin": origin, "Access-Control-Request-Method": "GET"}, timeout=10),
    ):
        assert "Access-Control-Allow-Origin" not in response.headers
        assert "Access-Control-Allow-Credentials" not in response.headers
        assert "Origin" in vary_values(response)


def test_request_without_origin_gets_no_cors_headers(cors_url):
    response = requests.post(f"{cors_url}/login", json={}, timeout=10)
    assert "Access-Control-Allow-Origin" not in response.headers
    assert "Access-Control-Allow-Credentials" not in response.headers


def test_default_config_allows_no_origin(base_url, server):
    response = requests.post(f"{base_url}/login", json={}, headers={"Origin": ALLOWED}, timeout=10)
    assert "Access-Control-Allow-Origin" not in response.headers
