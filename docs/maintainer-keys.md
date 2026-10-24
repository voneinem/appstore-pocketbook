# Maintainer guide: the signing key, publishing and branch protection

The catalog is signed with an Ed25519 key so that every consumer can tell that it was published by
the maintainer. This guide covers how the key is created, stored and rotated, how the first catalog
is published, and the repository settings that protect the process.

The private key is **never** in the repository. It exists only as the CI secret
`PBSTORE_SIGNING_KEY` (inside the `catalog-signing` environment) and in an **offline backup**. The
public keys are in `keys/trusted/` (see [keys/README.md](../keys/README.md)).

## 1. Create the key

Create the key on a machine you trust, in a folder that is **not** a git work tree (`keygen` refuses
to write the private key inside one):

```bash
pbstore-catalog keygen --private-out ~/pbstore-keys/current.key --public-out ~/pbstore-keys/current.pub
```

It prints the public key and writes the private key with mode 0600. Then:

1. Store the content of `current.key` as the secret `PBSTORE_SIGNING_KEY` in the GitHub environment
   `catalog-signing` (Settings, Environments; restrict the environment to the `main` branch).
2. Put an **offline backup** of `current.key` somewhere safe (an encrypted drive, a password
   manager's secure note), then delete the local copy.
3. Commit only the public half: `cp ~/pbstore-keys/current.pub keys/trusted/current.pub` and open a
   pull request.

Never paste the private key into an issue, a pull request, a chat or a workflow log.

## 2. Set up GitHub

1. **GitHub Pages**: Settings, Pages, source "GitHub Actions". The catalog is served at
   `https://<owner>.github.io/<repository>/catalog.json` (with `catalog.json.sig` and `keys/*.pub`).
   This address is compiled into the store and the installer, so it must not change lightly.
2. **Environments**: `catalog-signing` (holds the secret, limited to `main`) and `github-pages`.
3. **Branch protection for `main`**, because the pull-request check runs the validation code of the
   pull request itself:
   - require the status check `pull-request / check`;
   - require branches to be **up to date** before merging, so two pull requests cannot both pass
     against a stale tree;
   - require review from **Code Owners** (see `.github/CODEOWNERS`: the maintainer owns everything
     except `catalog/entries/`);
   - restrict who can push to `main`.
4. Pull requests from forks get a read-only token and no secrets; they can be validated safely but
   can never sign or publish.

## 3. The first publication

There is no live catalog yet, so the first run must say so explicitly:

1. Run the workflow **publish** manually (Actions, publish, Run workflow) with the input
   `first_publish` switched on. It builds the catalog from `catalog/entries/`, signs it, verifies it,
   scans it for key material, and deploys it to GitHub Pages.
2. Check the result:

   ```bash
   curl -fsS https://<owner>.github.io/<repository>/catalog.json -o catalog.json
   curl -fsS https://<owner>.github.io/<repository>/catalog.json.sig -o catalog.json.sig
   pbstore-catalog verify --catalog catalog.json --sig catalog.json.sig --keys keys/trusted
   ```

3. Remove `--first-publish` from `.github/workflows/pull-request.yml` (two places, marked with a
   comment) so the pull-request check always compares with the live catalog. Later runs of the
   publish workflow need no input.
4. Record the final catalog address in the README and in `docs/contracts/ci-workflows.md`.

## 4. How publishing works afterwards

Every merge to `main` runs the publish workflow: it verifies the live catalog, carries unchanged
entries over (their recorded size and hash are not recomputed), downloads new or changed packages,
builds the catalog with a higher serial, signs it, verifies the result (signature, schema and serial
greater than the live one), scans the files for key material and deploys them as one unit.

If **any** entry fails its checks, the whole run stops: nothing is published, the previous catalog
stays online, and GitHub's failed-run notification tells you. The job summary lists every failing
entry with its rule and value. Fix the entry with a further merge; no other change is published
until you do. (The pull-request check validates the merged tree with `build --dry-run`, so this is
rare.)

## 5. Key rotation

The store trusts a short list of public keys: the **current** key and the **next** key. Rotation
moves signing from the current to the next key without ever failing a consumer:

1. **Create the next key** as in section 1 (name it `next.key`/`next.pub`) and add its public half
   to `keys/trusted/` with a pull request. Do not change the signing secret yet.
2. **Ship it**: release a store version (and installer) whose trusted list contains both keys.
   Catalogs are still signed with the current key, which every store accepts.
3. **Wait** until stores that contain the next key are the ones in use.
4. **Switch the secret** `PBSTORE_SIGNING_KEY` to the next key and run the publish workflow. Stores
   that trust both keys verify the new catalog; a store that was never updated rejects it with
   `E-CAT-01` (it fails closed instead of trusting an unknown key).
5. **Retire the old key later**: remove the old public key from `keys/trusted/` and from the next
   store release, and create the following "next" key.

The rotation is rehearsed by the test `tests/integration/test_key_rotation.cpp`.

## 6. A lost or exposed key

- **Exposed** (someone else may have the private key): treat every catalog signed since as
  untrusted. Create a replacement key, publish an **emergency** store release that trusts only the
  replacement, switch the secret, publish. Remove the exposed key from `keys/trusted/`. Tell users to
  update; stores that do not update keep rejecting new catalogs and keep what they had.
- **Lost** (the secret and the backup are gone): you cannot sign any more. Create a new key and
  publish an emergency store release that trusts it, as above. Stores that trusted a "next" key that
  you still have can switch to it without a new release.
- In both cases update the secret in the `catalog-signing` environment and review who has access to
  the repository settings.

## 7. Checklist for a new maintainer

- [ ] `catalog-signing` environment exists, limited to `main`, with the secret set.
- [ ] An offline backup of the private key exists and was tested by reading it back.
- [ ] `keys/trusted/` holds the current (and the next) public key.
- [ ] Branch protection as in section 2, including review from Code Owners.
- [ ] GitHub Pages source is "GitHub Actions".
- [ ] The first publication was done and `--first-publish` removed from the pull-request workflow.
