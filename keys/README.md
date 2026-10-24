# Trusted public keys

`keys/trusted/*.pub` holds the public keys that verify the signature of the catalog. Normally there
are two: the **current** key that signs today, and the **next** key that will sign after the next
rotation. Consumers (the store, the installer, `pbstore-catalog verify`) compile in or read this list
and accept a catalog if **any** of these keys verifies its signature.

## Format

One file per key, `<name>.pub`, one line: the base64 of the 32-byte Ed25519 public key.

```text
mTyW1u9fR0xq3o8d7c5b2n4kLs6e1aZ+PqXvGhJwYtI=
```

(The line above is an illustration, not a real key.)

## Checking a catalog by hand

```bash
pbstore-catalog verify --catalog catalog.json --sig catalog.json.sig --keys keys/trusted
```

Exit code 0 means the signature is valid for one of the keys, the schema is supported and the
catalog is well formed. Add `--last-serial N` to also check that the catalog is not older than the
one you accepted before.

## Where the keys are published

The same files are published next to the catalog:
`https://<owner>.github.io/<repository>/keys/<name>.pub`. The copy in this repository is the
authoritative one; changes to this folder need the maintainer's review.

## Rotation and the private key

How the key is created, stored and rotated is described in `docs/maintainer-keys.md`. The private
key is never in this repository: it exists only as the CI secret `PBSTORE_SIGNING_KEY` and in an
offline backup.
