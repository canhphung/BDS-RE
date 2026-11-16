#!/usr/bin/env python3
"""Generate signed protocol-282 login fixtures with an independent ES384 stack.

Uses the Python `cryptography` package (OpenSSL), not the server's Mbed TLS,
so tests cross-check two implementations. Output is committed as
login_fixtures.json; CI only reads it. Keys are throwaway test keys. The
"root" key stands in for the Mojang root key, which cannot sign test data.

The chain shape mirrors what the reverse-engineered verifier accepts
(UnverifiedCertificate::verify 0x19e41d0): a client-signed token naming the
root as certificate authority, a root-signed token naming an intermediate, and
an intermediate-signed leaf with extraData and the client identity key.
"""
import base64, json, sys
from pathlib import Path
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.asymmetric.utils import decode_dss_signature

NBF, EXP = 1_000_000_000, 2_000_000_000  # 2001-09-09 .. 2033-05-18


def b64url(data):
    return base64.urlsafe_b64encode(data).rstrip(b"=").decode()


def public(key):
    der = key.public_key().public_bytes(serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo)
    return base64.b64encode(der).decode()


def jwt(signer, payload, alg="ES384", x5u=None):
    header = {"alg": alg, "x5u": x5u or public(signer)}
    signing_input = b64url(json.dumps(header, separators=(",", ":")).encode()) + "." + \
        b64url(json.dumps(payload, separators=(",", ":")).encode())
    r, s = decode_dss_signature(signer.sign(signing_input.encode(), ec.ECDSA(hashes.SHA384())))
    return signing_input + "." + b64url(r.to_bytes(48, "big") + s.to_bytes(48, "big"))


def chain(*tokens):
    return json.dumps({"chain": list(tokens)})


root, intermediate, client, stranger = (ec.generate_private_key(ec.SECP384R1()) for _ in range(4))
times = {"nbf": NBF, "exp": EXP}
identity = {"XUID": "2535400000000001", "identity": "8f6c3c4a-1b2e-4d5f-9a7b-0c1d2e3f4a5b", "displayName": "Steve"}
client_data = {"ServerAddress": "127.0.0.1:19132", "SkinId": "Standard_Custom", "LanguageCode": "en_US"}

t1 = jwt(client, {"certificateAuthority": True, "identityPublicKey": public(root), **times})
t2 = jwt(root, {"certificateAuthority": True, "identityPublicKey": public(intermediate), **times})
t3 = jwt(intermediate, {"extraData": identity, "identityPublicKey": public(client), **times})
client_token = jwt(client, client_data)

offline_identity = {"identity": "11111111-2222-4333-8444-555555555555", "displayName": "Offline"}
offline = jwt(client, {"extraData": offline_identity, "identityPublicKey": public(client), **times})

fixtures = {
    "generator": "tests/protocol/make_login_fixtures.py (python cryptography, ES384)",
    "now": 1_500_000_000,
    "trusted_root": public(root),
    "identity": identity,
    "offline_identity": offline_identity,
    "client_token": client_token,
    "chain": chain(t1, t2, t3),
    "self_signed_chain": chain(offline),
    "cases": {
        # Leaf signed by a key the CA token never named.
        "untrusted_leaf_signer": chain(t1, t2, jwt(stranger, {"extraData": identity, "identityPublicKey": public(client), **times},
                                                   x5u=public(intermediate))),
        # Intermediate token without certificateAuthority cannot extend trust.
        "intermediate_not_ca": chain(t1, jwt(root, {"identityPublicKey": public(intermediate), **times}), t3),
        "expired_leaf": chain(t1, t2, jwt(intermediate, {"extraData": identity, "identityPublicKey": public(client),
                                                          "nbf": NBF, "exp": 1_200_000_000})),
        "missing_nbf": chain(t1, t2, jwt(intermediate, {"extraData": identity, "identityPublicKey": public(client), "exp": EXP})),
        "es256_header": chain(t1, t2, jwt(intermediate, {"extraData": identity, "identityPublicKey": public(client), **times},
                                          alg="ES256")),
        "client_token_wrong_key": jwt(stranger, client_data, x5u=public(client)),
    },
}
Path(sys.argv[1] if len(sys.argv) > 1 else "tests/protocol/login_fixtures.json").write_text(json.dumps(fixtures, indent=2) + "\n")
