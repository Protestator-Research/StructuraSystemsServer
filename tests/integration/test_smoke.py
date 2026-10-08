import requests

from conftest import auth


def test_admin_login(admin_token):
    assert admin_token


def test_me(base_url, admin_token):
    response = requests.get(f"{base_url}/me", headers=auth(admin_token), timeout=10)
    assert response.status_code == 200
    assert response.json()["username"] == "admin"


def test_projects_list(base_url, admin_token):
    response = requests.get(f"{base_url}/projects", headers=auth(admin_token), timeout=30)
    assert response.status_code == 200
    assert isinstance(response.json(), list)
