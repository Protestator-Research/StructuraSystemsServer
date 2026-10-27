import pytest
import requests
from pymongo import MongoClient
from pymongo.errors import OperationFailure

from conftest import ADMIN_PASSWORD, MONGO_URI, login, start_server

ENC_DB = "structura_systems_enctest"
ENC_PORT = 18096
ENC_USER = "structura_test_enc"
ENC_PASSWORD = "p@ss:w/rd%#?"


def test_special_characters_in_credentials_are_encoded():
    client = MongoClient(MONGO_URI, serverSelectionTimeoutMS=5000)
    try:
        try:
            client.admin.command("createUser", ENC_USER, pwd=ENC_PASSWORD,
                                 roles=[{"role": "readWrite", "db": ENC_DB}])
        except OperationFailure as error:
            pytest.skip(f"cannot create users with the test credentials: {error}")

        config = {"mongo_username": ENC_USER, "mongo_password": ENC_PASSWORD}
        with start_server(ENC_DB, ENC_PORT, custom_config=config):
            token = login(f"http://127.0.0.1:{ENC_PORT}", "admin", ADMIN_PASSWORD)
            response = requests.get(f"http://127.0.0.1:{ENC_PORT}/me",
                                    headers={"Authorization": f"Bearer {token}"}, timeout=10)
            assert response.status_code == 200
    finally:
        client.drop_database(ENC_DB)
        try:
            client.admin.command("dropUser", ENC_USER)
        except OperationFailure:
            pass
        client.close()
