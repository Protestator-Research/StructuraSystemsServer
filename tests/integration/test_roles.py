import uuid

import pytest
import requests

from conftest import USER_PASSWORD, auth, login


def create(base_url, token, **body):
    return requests.post(f"{base_url}/admin/users", json=body, headers=auth(token), timeout=10)


def new_name():
    return f"user_{uuid.uuid4().hex[:12]}"


def get_users(base_url, token):
    response = requests.get(f"{base_url}/admin/users", headers=auth(token), timeout=10)
    return response


def user_exists(base_url, admin_token, username):
    users = get_users(base_url, admin_token).json()
    return username in [u["username"] for u in users]


def user_info(base_url, admin_token, username):
    for user in get_users(base_url, admin_token).json():
        if user["username"] == username:
            return user
    return None


def test_plain_user_cannot_manage_users(base_url, make_login):
    name, token = make_login()
    assert get_users(base_url, token).status_code == 403
    response = create(base_url, token, username=new_name(), password=USER_PASSWORD, role=0)
    assert response.status_code == 403
    assert requests.put(f"{base_url}/admin/users/{name}", json={"role": 1}, headers=auth(token), timeout=10).status_code == 403
    assert requests.delete(f"{base_url}/admin/users/{name}", headers=auth(token), timeout=10).status_code == 403


def test_moderator_creates_and_deletes_low_roles(base_url, admin_token, make_login):
    _, moderator = make_login(role=2)
    listing = get_users(base_url, moderator)
    assert listing.status_code == 200
    assert "admin" in [u["username"] for u in listing.json()]

    for role in (0, 1):
        name = new_name()
        assert create(base_url, moderator, username=name, password=USER_PASSWORD, role=role).status_code == 201
        assert user_info(base_url, admin_token, name)["role"] == role

    for role in (2, 100):
        name = new_name()
        assert create(base_url, moderator, username=name, password=USER_PASSWORD, role=role).status_code == 403
        assert not user_exists(base_url, admin_token, name)

    name = new_name()
    assert create(base_url, moderator, username=name, password=USER_PASSWORD).status_code == 201
    assert requests.delete(f"{base_url}/admin/users/{name}", headers=auth(moderator), timeout=10).status_code == 200
    assert not user_exists(base_url, admin_token, name)


def test_moderator_cannot_touch_admins_or_moderators(base_url, admin_token, make_login):
    _, moderator = make_login(role=2)
    other_name, _ = make_login(role=2)
    url = f"{base_url}/admin/users"
    assert requests.put(f"{url}/admin", json={"group": "x"}, headers=auth(moderator), timeout=10).status_code == 403
    assert requests.delete(f"{url}/admin", headers=auth(moderator), timeout=10).status_code == 403
    assert requests.put(f"{url}/{other_name}", json={"group": "x"}, headers=auth(moderator), timeout=10).status_code == 403
    assert requests.delete(f"{url}/{other_name}", headers=auth(moderator), timeout=10).status_code == 403
    assert user_exists(base_url, admin_token, "admin")


def test_moderator_cannot_promote(base_url, make_login):
    _, moderator = make_login(role=2)
    name, _ = make_login()
    url = f"{base_url}/admin/users/{name}"
    for role in (2, 100):
        assert requests.put(url, json={"role": role}, headers=auth(moderator), timeout=10).status_code == 403
    assert requests.put(url, json={"role": 1}, headers=auth(moderator), timeout=10).status_code == 200


def test_group_manager_membership(base_url, admin_token, make_login, make_user):
    group = f"grp{uuid.uuid4().hex[:6]}"
    other_group = f"oth{uuid.uuid4().hex[:6]}"
    manager, manager_token = make_login(role=1, group=group)
    member, member_password = make_user()
    member_token = login(base_url, member, member_password)
    in_other, _ = make_user(group=other_group)
    members = f"{base_url}/groups/{group}/members"

    # Add a groupless user.
    response = requests.post(f"{members}/{member}", headers=auth(manager_token), timeout=10)
    assert response.status_code == 200, response.text
    assert requests.get(f"{base_url}/me", headers=auth(member_token), timeout=10).json()["group"] == group

    # Not into a foreign group, not a user who already has another group.
    foreign = requests.post(f"{base_url}/groups/{other_group}/members/{member}", headers=auth(manager_token), timeout=10)
    assert foreign.status_code == 403
    taken = requests.post(f"{members}/{in_other}", headers=auth(manager_token), timeout=10)
    assert taken.status_code in (403, 409)
    assert user_info(base_url, admin_token, in_other)["group"] == other_group

    # Listing: own group only.
    listing = requests.get(members, headers=auth(manager_token), timeout=10)
    assert listing.status_code == 200
    assert {u["username"] for u in listing.json()} == {manager, member}
    assert requests.get(f"{base_url}/groups/{other_group}/members", headers=auth(manager_token), timeout=10).status_code == 403
    scoped = get_users(base_url, manager_token)
    assert scoped.status_code == 200
    assert {u["username"] for u in scoped.json()} == {manager, member}

    # No account management.
    assert requests.delete(f"{base_url}/admin/users/{member}", headers=auth(manager_token), timeout=10).status_code == 403
    assert create(base_url, manager_token, username=new_name(), password=USER_PASSWORD).status_code == 403

    # Removal only clears the group.
    response = requests.delete(f"{members}/{member}", headers=auth(manager_token), timeout=10)
    assert response.status_code == 200, response.text
    info = user_info(base_url, admin_token, member)
    assert info is not None and info["group"] == ""
    assert requests.delete(f"{members}/{member}", headers=auth(manager_token), timeout=10).status_code == 404


def test_group_manager_cannot_remove_foreign_members(base_url, make_login, make_user):
    group = f"grp{uuid.uuid4().hex[:6]}"
    _, manager_token = make_login(role=1, group=group)
    victim, _ = make_user(group="elsewhere")
    response = requests.delete(f"{base_url}/groups/{group}/members/{victim}", headers=auth(manager_token), timeout=10)
    assert response.status_code == 404
    response = requests.delete(f"{base_url}/groups/elsewhere/members/{victim}", headers=auth(manager_token), timeout=10)
    assert response.status_code == 403


def test_plain_user_cannot_manage_groups(base_url, make_login, make_user):
    group = f"grp{uuid.uuid4().hex[:6]}"
    _, token = make_login(group=group)
    other, _ = make_user()
    assert requests.get(f"{base_url}/groups/{group}/members", headers=auth(token), timeout=10).status_code == 403
    assert requests.post(f"{base_url}/groups/{group}/members/{other}", headers=auth(token), timeout=10).status_code == 403


def test_admin_cannot_delete_self_or_demote_last_admin(base_url, admin_token):
    url = f"{base_url}/admin/users/admin"
    assert requests.delete(url, headers=auth(admin_token), timeout=10).status_code == 403
    assert requests.put(url, json={"role": 1}, headers=auth(admin_token), timeout=10).status_code == 409
    me = requests.get(f"{base_url}/me", headers=auth(admin_token), timeout=10)
    assert me.status_code == 200 and me.json()["role"] == 100


def test_put_changes_role_and_keeps_user(base_url, admin_token, make_user):
    name, password = make_user()
    old_token = login(base_url, name, password)
    response = requests.put(f"{base_url}/admin/users/{name}", json={"role": 1}, headers=auth(admin_token), timeout=10)
    assert response.status_code == 200, response.text
    assert user_info(base_url, admin_token, name)["role"] == 1
    # A role change invalidates the old tokens; the account is still usable.
    assert requests.get(f"{base_url}/me", headers=auth(old_token), timeout=10).status_code == 401
    assert login(base_url, name, password)


def test_put_changes_only_given_fields_and_clears_group(base_url, admin_token, make_user):
    name, _ = make_user(role=1, group="keepme")
    url = f"{base_url}/admin/users/{name}"
    assert requests.put(url, json={}, headers=auth(admin_token), timeout=10).status_code == 200
    info = user_info(base_url, admin_token, name)
    assert (info["role"], info["group"]) == (1, "keepme")
    assert requests.put(url, json={"group": ""}, headers=auth(admin_token), timeout=10).status_code == 200
    info = user_info(base_url, admin_token, name)
    assert (info["role"], info["group"]) == (1, "")
    assert requests.put(url, json={"group": "newgrp"}, headers=auth(admin_token), timeout=10).status_code == 200
    assert user_info(base_url, admin_token, name)["group"] == "newgrp"


def test_unknown_user_returns_404(base_url, admin_token):
    url = f"{base_url}/admin/users/doesnotexist"
    assert requests.put(url, json={"role": 1}, headers=auth(admin_token), timeout=10).status_code == 404
    assert requests.delete(url, headers=auth(admin_token), timeout=10).status_code == 404


@pytest.mark.parametrize("body", [
    {"password": "short"},
    {"username": "ab"},
    {"username": "bad name!"},
    {"role": 5},
    {"role": "admin"},
    {"group": "bad group!"},
    {"password": USER_PASSWORD},
    {"username": "x" * 65},
], ids=["short_password", "short_username", "bad_username", "bad_role", "role_string",
        "bad_group", "missing_username", "long_username"])
def test_create_validation(base_url, admin_token, body):
    payload = {"username": new_name(), "password": USER_PASSWORD, "role": 0}
    payload.update(body)
    if "username" not in body and body.keys() == {"password"}:
        del payload["username"]
    response = create(base_url, admin_token, **payload)
    assert response.status_code == 400, response.text


def test_create_requires_password(base_url, admin_token):
    assert create(base_url, admin_token, username=new_name()).status_code == 400


def test_update_validation(base_url, admin_token, make_user):
    name, _ = make_user()
    url = f"{base_url}/admin/users/{name}"
    for body in ({"password": "short"}, {"role": 5}, {"group": "bad group"}):
        assert requests.put(url, json=body, headers=auth(admin_token), timeout=10).status_code == 400


def test_duplicate_user_conflicts(base_url, admin_token, make_user):
    name, _ = make_user()
    assert create(base_url, admin_token, username=name, password=USER_PASSWORD).status_code == 409


def test_create_with_group(base_url, admin_token):
    name = new_name()
    assert create(base_url, admin_token, username=name, password=USER_PASSWORD, group="grp1").status_code == 201
    assert user_info(base_url, admin_token, name)["group"] == "grp1"


def test_deleted_user_token_is_invalid(base_url, admin_token, make_user):
    name, password = make_user()
    token = login(base_url, name, password)
    assert requests.get(f"{base_url}/me", headers=auth(token), timeout=10).status_code == 200
    assert requests.delete(f"{base_url}/admin/users/{name}", headers=auth(admin_token), timeout=10).status_code == 200
    assert requests.get(f"{base_url}/me", headers=auth(token), timeout=10).status_code == 401
    assert requests.get(f"{base_url}/projects", headers=auth(token), timeout=10).status_code == 401
    response = requests.post(f"{base_url}/login", json={"username": name, "password": password}, timeout=10)
    assert response.status_code == 401


def test_password_change_by_admin_invalidates_tokens(base_url, admin_token, make_user):
    name, password = make_user()
    token = login(base_url, name, password)
    new_password = "AnotherPass1234!"
    response = requests.put(f"{base_url}/admin/users/{name}", json={"password": new_password},
                            headers=auth(admin_token), timeout=10)
    assert response.status_code == 200
    assert requests.get(f"{base_url}/me", headers=auth(token), timeout=10).status_code == 401
    assert requests.post(f"{base_url}/login", json={"username": name, "password": password}, timeout=10).status_code == 401
    assert login(base_url, name, new_password)
