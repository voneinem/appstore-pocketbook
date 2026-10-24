#!/usr/bin/env bash
# Local merge gate: everything that must be green before a change is merged
# (constitution, Development Workflow: tests pass and no new secrets).
#
#   scripts/pre-merge.sh                 build the host target, run all tests, scan for secrets
#   scripts/pre-merge.sh --sanitize      the same with AddressSanitizer and UndefinedBehaviorSanitizer
#   scripts/pre-merge.sh --secrets [DIR] only the secret scan, of the repository or of DIR
#                                        (the publish workflow scans the directory that becomes the
#                                        Pages artifact; if PBSTORE_SIGNING_KEY is set, DIR must not
#                                        contain its value either)
#   scripts/pre-merge.sh --self-test     prove that the built-in secret scan detects what it should
#
# It also runs scripts/check-version-bump.sh (a branch that changes the product must raise VERSION
# and add a CHANGELOG.md entry) and scripts/check-constitution.sh (TLS bypasses, SDK paths, private
# keys, Qt in the core, workflow rules).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# ---- secret scan ---------------------------------------------------------------------------------
# Uses gitleaks when installed, otherwise built-in patterns. In a repository, tests/, third_party/,
# specs/ and the checker scripts (whose self-tests embed fake bad samples) are excluded from the
# built-in scan. A directory that is not a git repository (an artifact) is scanned completely.
secret_scan() {
    local root="$1"
    if command -v gitleaks >/dev/null 2>&1 && [ -d "$root/.git" ]; then
        echo "secret scan: gitleaks"
        gitleaks detect --no-git --source "$root" --no-banner --redact || return 1
    else
        echo "secret scan: built-in patterns"
    fi
    local bad=0 f p repo=0
    [ -d "$root/.git" ] && repo=1
    while IFS= read -r -d '' f; do
        if [ $repo -eq 1 ]; then
            case "$f" in
                third_party/*|tests/*|specs/*|.specify/*|.git/*|build*|.claude/*|*.bmp|*.zip) continue ;;
                scripts/pre-merge.sh|scripts/check-*.sh) continue ;;
            esac
        fi
        p="$root/$f"
        [ -f "$p" ] && grep -Iq . "$p" 2>/dev/null || continue
        if grep -nE -e '-----BEGIN ([A-Z0-9]+ )*PRIVATE KEY-----' \
                    -e 'Authorization:[[:space:]]*Basic[[:space:]]+[A-Za-z0-9+/=]{8,}' \
                    -e '(password|passwd|secret|api[_-]?key|token)[[:space:]]*[:=][[:space:]]*"[^"$<{][^"]{5,}"' \
                    "$p" | grep -vE '(example|placeholder|REDACTED|<|\$\{)' | sed "s#^#SECRET $f:#" | grep .; then
            bad=1
        fi
    done < <(if [ -d "$root/.git" ]; then git -C "$root" ls-files -z --cached --others --exclude-standard; else (cd "$root" && find . -type f -not -path './.git/*' -print0 | sed -z 's#^\./##'); fi)
    # the actual signing key must not appear anywhere in the scanned files (names only, never the value)
    if [ -n "${PBSTORE_SIGNING_KEY:-}" ]; then
        local hits
        hits="$(grep -rlF -e "$PBSTORE_SIGNING_KEY" "$root" 2>/dev/null | grep -v '/\.git/' || true)"
        if [ -n "$hits" ]; then
            printf 'SECRET the signing key value appears in: %s\n' "$hits"
            bad=1
        fi
    fi
    return $bad
}

if [ "${1:-}" = "--self-test" ]; then
    tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
    fail=0
    check() {  # check <pass|fail> <description>
        local rc=0
        secret_scan "$tmp" >/dev/null 2>&1 || rc=$?
        if { [ "$1" = pass ] && [ $rc -eq 0 ]; } || { [ "$1" = fail ] && [ $rc -ne 0 ]; }; then echo "ok   $2"; else echo "FAIL $2 (rc=$rc)"; fail=1; fi
    }
    # gitleaks would take over; the self-test always exercises the built-in patterns
    if command -v gitleaks >/dev/null 2>&1; then echo "note: gitleaks present, built-in self-test skipped"; exit 0; fi
    printf 'int main() { return 0; }\n' > "$tmp/a.cpp"
    check pass "clean tree"
    printf -- '-----BEGIN RSA PRIVATE KEY-----\nMIIE\n' > "$tmp/k.pem.txt"; check fail "private key"; rm "$tmp/k.pem.txt"
    printf -- '-----BEGIN PRIVATE KEY-----\nMC4C\n' > "$tmp/k.txt"; check fail "PKCS8 private key"; rm "$tmp/k.txt"
    printf 'Authorization: Basic YWxpY2U6cHdzZWNyZXQ=\n' > "$tmp/h.txt"; check fail "basic auth header"; rm "$tmp/h.txt"
    printf 'const char* password = "hunter2hunter2";\n' > "$tmp/p.cpp"; check fail "hard-coded password"; rm "$tmp/p.cpp"
    printf 'password = "<your app password>"\n' > "$tmp/ok.txt"; check pass "placeholder password is fine"; rm "$tmp/ok.txt"
    # artifact mode: the signing key value must not be in the files
    printf '{"serial": 1}\n' > "$tmp/catalog.json"
    PBSTORE_SIGNING_KEY="c2VlZC1mb3ItdGVzdC1vbmx5LW5vdC1hLXJlYWwta2V5" check pass "artifact without the key value"
    printf 'leak c2VlZC1mb3ItdGVzdC1vbmx5LW5vdC1hLXJlYWwta2V5 leak\n' > "$tmp/notes.txt"
    PBSTORE_SIGNING_KEY="c2VlZC1mb3ItdGVzdC1vbmx5LW5vdC1hLXJlYWwta2V5" check fail "artifact containing the key value"
    rm "$tmp/notes.txt"
    # the other gates carry their own self-tests
    "$ROOT/scripts/check-version-bump.sh" --self-test >/dev/null || { echo "FAIL check-version-bump self-test"; fail=1; }
    "$ROOT/scripts/check-constitution.sh" --self-test >/dev/null || { echo "FAIL check-constitution self-test"; fail=1; }
    [ $fail -eq 0 ] && echo "ok   the other gate scripts pass their self-tests"
    exit $fail
fi

if [ "${1:-}" = "--secrets" ]; then
    secret_scan "$(cd "${2:-$ROOT}" && pwd)"
    exit $?
fi

SANITIZE=OFF
BUILD_DIR="$ROOT/build-premerge"
if [ "${1:-}" = "--sanitize" ]; then SANITIZE=ON; BUILD_DIR="$ROOT/build-sanitize"; fi

echo "== version bump"
"$ROOT/scripts/check-version-bump.sh" "$ROOT"

echo "== constitution checks"
"$ROOT/scripts/check-constitution.sh" "$ROOT"

echo "== host build and tests (sanitizers: $SANITIZE)"
cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Debug -DPBSTORE_SANITIZE=$SANITIZE >/dev/null
cmake --build "$BUILD_DIR"
ctest --test-dir "$BUILD_DIR" --output-on-failure

echo "== secret scan"
secret_scan "$ROOT"
echo "all checks passed"
