import requests

from conftest import USER_PASSWORD, auth, login

NEW_PASSWORD = "BrandNewPass123!"


def reset_by_admin(base_url, admin_token, name, password="ResetByAdmin123!"):
    response = requests.put(f"{base_url}/admin/users/{name}", json={"password": password},
                            headers=auth(admin_token), timeout=10)
    assert response.status_code == 200, response.text
    return password


def test_admin_reset_requires_password_change(base_url, admin_token, make_user):
    name, _ = make_user()
    password = reset_by_admin(base_url, admin_token, name)
    token = login(base_url, name, password)

    me = requests.get(f"{base_url}/me", headers=auth(token), timeout=10)
    assert me.status_code == 200
    assert me.json()["mustChangePassword"] is True

    blocked = requests.get(f"{base_url}/projects", headers=auth(token), timeout=10)
    assert blocked.status_code == 403
    assert blocked.json() == {"error": "password_change_required"}
    assert requests.get(f"{base_url}/admin/users", headers=auth(token), timeout=10).status_code == 403


def test_created_user_does_not_need_change(base_url, make_login):
    _, token = make_login()
    me = requests.get(f"{base_url}/me", headers=auth(token), timeout=10).json()
    assert me["mustChangePassword"] is False
    assert requests.get(f"{base_url}/projects", headers=auth(token), timeout=30).status_code == 200


def test_change_password_flow(base_url, admin_token, make_user):
    name, _ = make_user()
    password = reset_by_admin(base_url, admin_token, name)
    token = login(base_url, name, password)
    url = f"{base_url}/me/password"

    wrong = requests.put(url, json={"oldPassword": "WrongWrong1234!", "newPassword": NEW_PASSWORD},
                         headers=auth(token), timeout=10)
    assert wrong.status_code in (401, 403)
    assert requests.get(f"{base_url}/projects", headers=auth(token), timeout=10).status_code == 403

    short = requests.put(url, json={"oldPassword": password, "newPassword": "short"}, headers=auth(token), timeout=10)
    assert short.status_code == 400
    assert requests.put(url, json={"oldPassword": password}, headers=auth(token), timeout=10).status_code == 400

    ok = requests.put(url, json={"oldPassword": password, "newPassword": NEW_PASSWORD}, headers=auth(token), timeout=10)
    assert ok.status_code == 200, ok.text
    # The current token stays valid and is no longer restricted.
    assert requests.get(f"{base_url}/projects", headers=auth(token), timeout=30).status_code == 200
    assert requests.get(f"{base_url}/me", headers=auth(token), timeout=10).json()["mustChangePassword"] is False

    assert requests.post(f"{base_url}/login", json={"username": name, "password": password}, timeout=10).status_code == 401
    assert login(base_url, name, NEW_PASSWORD)


def test_regular_user_can_change_password(base_url, make_user):
    name, password = make_user()
    token = login(base_url, name, password)
    response = requests.put(f"{base_url}/me/password", json={"oldPassword": password, "newPassword": NEW_PASSWORD},
                            headers=auth(token), timeout=10)
    assert response.status_code == 200
    assert login(base_url, name, NEW_PASSWORD)


def test_password_change_requires_authentication(base_url):
    response = requests.put(f"{base_url}/me/password", json={"oldPassword": "a", "newPassword": NEW_PASSWORD}, timeout=10)
    assert response.status_code == 401
