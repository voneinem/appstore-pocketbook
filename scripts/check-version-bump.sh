#!/usr/bin/env bash
# Release-version gate: a branch that changes a product must raise its version (constitution, Development
# Workflow) and say what changed in CHANGELOG.md. There are two products: the installer (VERSION, heading
# '## <version>') and the store app on the reader (app/VERSION, heading '## store-<version>').
#
#   scripts/check-version-bump.sh [repo-root]    compare the working tree with the base branch
#   scripts/check-version-bump.sh --self-test    prove the rules on throw-away repositories
#
# Environment: PBSTORE_BASE_REF  branch to compare with (default: main, else origin/main)
#              PBSTORE_NO_BUMP=1 allow a product change without a bump (refactor, no visible change)
#
# A base without a VERSION file (the branch introduces it) needs no bump, only the CHANGELOG entry.
# Files that do not need a bump: specs/, docs/, .specify/, .claude/, *.md except CHANGELOG.md, the app
# entries under catalog/entries/ (data added by contributors) and the trusted public keys under
# keys/trusted/ (configuration of who is trusted): none of them changes the tool.
set -euo pipefail

semver_gt() {  # semver_gt <new> <old>: new is strictly greater
    [ "$1" != "$2" ] && [ "$(printf '%s\n%s\n' "$1" "$2" | sort -V | tail -n 1)" = "$1" ]
}

# Store paths belong to the app on the reader (app/VERSION, tags store-v<version>, '## store-<version>').
# Shared paths are linked into both programs and need both bumps. Everything else that is not exempt
# belongs to the installer (VERSION, tags v<version>, '## <version>').
STORE_RE='^(app/|bridge/|qml/|platform/|packaging/|core/store/|tests/(store|bridge|walks|launcher|packaging)/|cmake/|res/|licenses/|scripts/(package-device|build-curl-arm|check-store-binary|check-arm-build|check-ui-style|check-release|ui-walk|run-host-demo)\.sh$)'
SHARED_RE='^(core/src/|core/include/|third_party/)'

check() {
    local root="$1" base="${PBSTORE_BASE_REF:-}" cur old new old_app new_app changed need_store=0 need_inst=0 bad=0
    cd "$root"
    git rev-parse --git-dir >/dev/null 2>&1 || { echo "version check: not a git repository, skipped"; return 0; }
    if [ -z "$base" ]; then
        for base in main origin/main; do git rev-parse --verify -q "$base" >/dev/null && break || base=""; done
    fi
    [ -n "$base" ] || { echo "version check: no main branch to compare with, skipped"; return 0; }
    cur="$(git rev-parse --abbrev-ref HEAD)"
    [ "$cur" != "$base" ] || { echo "version check: on $base itself, skipped"; return 0; }
    base="$(git merge-base "$base" HEAD)"

    new="$(head -n 1 VERSION 2>/dev/null | tr -d '[:space:]')"
    [[ "$new" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "version check FAILED: VERSION must contain MAJOR.MINOR.PATCH (got '$new')" >&2; return 1; }
    old="$(git show "$base:VERSION" 2>/dev/null | head -n 1 | tr -d '[:space:]' || true)"
    new_app=""; [ ! -f app/VERSION ] || new_app="$(head -n 1 app/VERSION | tr -d '[:space:]')"
    if [ -f app/VERSION ] && ! [[ "$new_app" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
        echo "version check FAILED: app/VERSION must contain MAJOR.MINOR.PATCH (got '$new_app')" >&2; return 1
    fi
    old_app="$(git show "$base:app/VERSION" 2>/dev/null | head -n 1 | tr -d '[:space:]' || true)"

    changed="$( { git diff --name-only "$base"; git ls-files --others --exclude-standard; } | sort -u \
        | grep -Ev '^(specs/|docs/|\.specify/|\.claude/|catalog/entries/|keys/trusted/)' | grep -Ev '(^|/)[^/]*\.md$' || true)"
    changed="$(printf '%s\n' "$changed" | grep -Evx 'VERSION|app/VERSION|CHANGELOG\.md|' || true)"
    if [ -n "$changed" ]; then
        printf '%s\n' "$changed" | grep -Eq "$STORE_RE|$SHARED_RE" && need_store=1
        printf '%s\n' "$changed" | grep -Ev "$STORE_RE" | grep -q . && need_inst=1
    fi

    if [ "${PBSTORE_NO_BUMP:-}" != 1 ]; then
        if [ $need_inst = 1 ] && [ -n "$old" ] && ! semver_gt "$new" "$old"; then
            echo "version check FAILED: the branch changes the installer but VERSION is still $new (base: $old)." >&2
            echo "Raise VERSION (MINOR for a feature, PATCH for a fix, MAJOR for a breaking change) and add a" >&2
            echo "CHANGELOG.md entry, or set PBSTORE_NO_BUMP=1 for a change nobody can see." >&2
            echo "Changed: $(printf '%s\n' "$changed" | grep -Ev "$STORE_RE" | head -n 5 | tr '\n' ' ')" >&2
            bad=1
        fi
        if [ $need_store = 1 ]; then
            if [ -z "$new_app" ]; then
                echo "version check FAILED: the branch changes the store app but app/VERSION does not exist." >&2; bad=1
            elif [ -n "$old_app" ] && ! semver_gt "$new_app" "$old_app"; then
                echo "version check FAILED: the branch changes the store app but app/VERSION is still $new_app (base: $old_app)." >&2
                echo "Raise app/VERSION and add a '## store-<version>' entry to CHANGELOG.md, or set PBSTORE_NO_BUMP=1." >&2
                echo "Changed: $(printf '%s\n' "$changed" | grep -E "$STORE_RE|$SHARED_RE" | head -n 5 | tr '\n' ' ')" >&2
                bad=1
            fi
        fi
    fi
    [ $bad = 0 ] || return 1
    if [ "$new" != "$old" ]; then   # also when the base has no VERSION yet
        if ! grep -Eq "^## \[?${new//./\.}\]?([[:space:]]|$)" CHANGELOG.md 2>/dev/null; then
            echo "version check FAILED: VERSION is $new but CHANGELOG.md has no '## $new' entry." >&2
            return 1
        fi
    fi
    if [ -n "$new_app" ] && [ "$new_app" != "$old_app" ]; then
        if ! grep -Eq "^## store-\[?${new_app//./\.}\]?([[:space:]]|$)" CHANGELOG.md 2>/dev/null; then
            echo "version check FAILED: app/VERSION is $new_app but CHANGELOG.md has no '## store-$new_app' entry." >&2
            return 1
        fi
    fi
    echo "version check passed (VERSION $new, base ${old:-none}; app/VERSION ${new_app:-none}, base ${old_app:-none})"
}

if [ "${1:-}" = "--self-test" ]; then
    tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
    fail=0
    expect() {  # expect <pass|fail> <description>
        local rc=0
        ( check "$tmp/r" ) >/dev/null 2>&1 || rc=$?
        if { [ "$1" = pass ] && [ $rc -eq 0 ]; } || { [ "$1" = fail ] && [ $rc -ne 0 ]; }; then echo "ok   $2"; else echo "FAIL $2 (rc=$rc)"; fail=1; fi
    }
    newrepo() {  # newrepo [with-app-version]
        rm -rf "$tmp/r"; mkdir "$tmp/r"; cd "$tmp/r"
        git init -q -b main . && git config user.email t@t && git config user.name t
        printf '1.0.0\n' > VERSION; printf '## 1.0.0\n- first\n' > CHANGELOG.md
        mkdir -p app specs tools core/src core/store scripts; echo a > app/a.cpp; echo t > tools/t.cpp; echo c > core/src/c.cpp
        echo s > core/store/s.cpp; echo p > scripts/package-device.sh; echo o > scripts/other.sh
        if [ "${1:-}" = with-app-version ]; then
            printf '0.1.0\n' > app/VERSION; printf '## store-0.1.0\n- first\n\n## 1.0.0\n- first\n' > CHANGELOG.md
        fi
        git add . && git commit -qm base
        git checkout -qb feature/x
    }
    # --- the installer product (VERSION, '## <version>') ---
    newrepo with-app-version
    expect pass "nothing changed"
    echo b > specs/s.md; echo c > README.md; expect pass "docs and specs only need no bump"
    mkdir -p catalog/entries; echo '{}' > catalog/entries/app.json; expect pass "an app entry is data and needs no bump"
    mkdir -p keys/trusted; echo 'AAAA' > keys/trusted/next.pub; expect pass "a trusted public key is configuration and needs no bump"
    rm -r catalog keys
    echo b > tools/t.cpp; expect fail "installer code changed, VERSION unchanged"
    PBSTORE_NO_BUMP=1 expect pass "explicit opt-out"
    printf '1.1.0\n' > VERSION; expect fail "installer: bumped but no CHANGELOG entry"
    printf '## 1.1.0\n- feature\n\n%s' "$(cat CHANGELOG.md)" > CHANGELOG.md; expect pass "installer: bumped with CHANGELOG entry"
    echo b > scripts/other.sh; expect pass "a general script belongs to the installer (VERSION raised)"
    printf '0.9.0\n' > VERSION; printf '## 0.9.0\n' > CHANGELOG.md; expect fail "version lowered"
    printf 'abc\n' > VERSION; expect fail "malformed VERSION"
    # --- the store product (app/VERSION, '## store-<version>') ---
    newrepo with-app-version
    echo b > app/a.cpp; expect fail "store code changed, app/VERSION unchanged"
    printf '1.1.0\n' > VERSION; printf '## 1.1.0\n\n%s' "$(cat CHANGELOG.md)" > CHANGELOG.md
    expect fail "store code changed, only the installer's VERSION raised"
    git checkout -q . ; git clean -fdq; git checkout -q main; git checkout -q feature/x
    echo b > app/a.cpp; printf '0.2.0\n' > app/VERSION; expect fail "store: app/VERSION raised but no '## store-' heading"
    printf '## store-0.2.0\n- feature\n\n%s' "$(cat CHANGELOG.md)" > CHANGELOG.md; expect pass "store: app/VERSION raised with heading, installer untouched"
    echo b > core/store/s.cpp; expect pass "core/store is a store path"
    echo b > scripts/package-device.sh; expect pass "package-device.sh is a store path"
    printf '0.0.9\n' > app/VERSION; printf '## store-0.0.9\n\n%s' "$(cat CHANGELOG.md)" > CHANGELOG.md; expect fail "app/VERSION lowered"
    printf 'abc\n' > app/VERSION; expect fail "malformed app/VERSION"
    newrepo with-app-version
    echo b > tools/t.cpp; printf '0.2.0\n' > app/VERSION; printf '## store-0.2.0\n\n%s' "$(cat CHANGELOG.md)" > CHANGELOG.md
    expect fail "installer code changed, only app/VERSION raised"
    echo b > app/RELEASE_DATE; git checkout -q VERSION CHANGELOG.md 2>/dev/null || true
    # --- shared code needs both ---
    newrepo with-app-version
    echo b > core/src/c.cpp; expect fail "shared code changed, neither raised"
    printf '1.1.0\n' > VERSION; printf '## 1.1.0\n\n%s' "$(cat CHANGELOG.md)" > CHANGELOG.md; expect fail "shared code changed, only VERSION raised"
    printf '0.2.0\n' > app/VERSION; printf '## store-0.2.0\n\n%s' "$(cat CHANGELOG.md)" > CHANGELOG.md; expect pass "shared code changed, both raised with headings"
    # --- a base without app/VERSION: the branch introduces the store ---
    newrepo
    echo b > app/a.cpp; printf '0.1.0\n' > app/VERSION; expect fail "base has no app/VERSION: '## store-' heading still required"
    printf '## store-0.1.0\n\n%s' "$(cat CHANGELOG.md)" > CHANGELOG.md; expect pass "base has no app/VERSION: introduced with heading"
    newrepo
    echo b > app/a.cpp; expect fail "store code changed and app/VERSION does not exist"
    git checkout -q main; expect pass "on main itself"
    # --- a base without VERSION: the branch introduces it ---
    rm -rf "$tmp/r"; mkdir "$tmp/r"; cd "$tmp/r"
    git init -q -b main . && git config user.email t@t && git config user.name t
    mkdir tools; echo a > tools/a.cpp; git add . && git commit -qm base
    git checkout -qb feature/y; printf '1.0.0\n' > VERSION; echo z > tools/z.cpp
    expect fail "base has no VERSION: CHANGELOG entry still required"
    printf '## 1.0.0\n' > CHANGELOG.md; expect pass "base has no VERSION: introduced with CHANGELOG"
    exit $fail
fi

check "$(cd "${1:-$(dirname "${BASH_SOURCE[0]}")/..}" && pwd)"
