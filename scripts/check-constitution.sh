#!/usr/bin/env bash
# Mechanical checks of the project constitution (run by scripts/pre-merge.sh).
#
#   scripts/check-constitution.sh [repo-root]    scan (default: this repository)
#   scripts/check-constitution.sh --self-test    prove that every rule below detects a violation
#
# Rules (a finding makes the script exit non-zero):
#   R1 TLS verification is never disabled, in code, CMake and shell scripts:
#        curl -k, curl --insecure, wget --no-check-certificate,
#        CURLOPT_SSL_VERIFYPEER / CURLOPT_SSL_VERIFYHOST set to 0 (VERIFYHOST must be 2),
#        CURLOPT_SSL_VERIFYSTATUS, CURLOPT_PROXY_SSL_VERIFYPEER, CURLOPT_PROXY_SSL_VERIFYHOST,
#        CURLOPT_DOH_SSL_VERIFYPEER, CURLOPT_DOH_SSL_VERIFYHOST, CURLOPT_DOH_SSL_VERIFYSTATUS
#        set to 0 / false.
#   R2 No hard-coded SDK, toolchain or home-directory locations ("pocketbook-sdk", "/opt/",
#        "/home/<user>/", "/Users/<user>/") in CMake files, scripts, sources and workflows.
#        The SDK location comes only from $POCKETBOOK_SDK or -DSDK_PATH.
#   R3 No private key material ("-----BEGIN ... PRIVATE KEY-----") outside tests/.
#   R4 core/ is Qt-free, InkView-free and SDK-free (constitution Principle VI).
#   R5 Every "uses:" in .github/workflows/*.yml is pinned to a full 40-character commit SHA
#        (local actions "./..." are allowed).
#   R6 publish.yml triggers only on push to main (plus workflow_dispatch), declares
#        "environment: catalog-signing", a "concurrency:" block with "cancel-in-progress: false"
#        and a "timeout-minutes:".
#   R7 No workflow uses pull_request_target.
#   R8 pull-request.yml references no "secrets.".
#   R9 No workflow step echoes the signing key or passes it on a command line.
#   R10 publish.yml never passes --package-dir or --now.
#   R11 publish.yml runs "pre-merge.sh --secrets" before the Pages deployment.
#   R12 pull-request.yml runs "check-pr-paths" and passes --live-url to check-entry.
set -u

SELF="$(basename "${BASH_SOURCE[0]}")"

# Lists the files to scan, relative to $1. Uses git when available, else find.
list_files() {
    local root="$1"
    if [ -d "$root/.git" ] && command -v git >/dev/null 2>&1; then
        git -C "$root" ls-files -z --cached --others --exclude-standard
    else
        (cd "$root" && find . -type f -not -path './.git/*' -not -path './build*' -print0 | sed -z 's#^\./##')
    fi
}

report() {  # report <rule> <file> <pattern> <path>: print matching lines, return 0 if any
    local rule="$1" f="$2" pat="$3" p="$4"
    if grep -nE -e "$pat" "$p" >/dev/null 2>&1; then
        grep -nE -e "$pat" "$p" | sed "s#^#$rule $f:#"
        return 0
    fi
    return 1
}

scan() {
    local root="$1" bad=0 f p
    while IFS= read -r -d '' f; do
        [ -f "$root/$f" ] || continue
        case "$f" in
            third_party/*|specs/*|.specify/*|.claude/*|"scripts/$SELF"|scripts/pre-merge.sh|*.bmp|*.zip|*.pem) continue ;;
        esac
        grep -Iq . "$root/$f" 2>/dev/null || continue   # skip binary/empty files
        p="$root/$f"

        case "$f" in
            core/*|tools/*|scripts/*|tests/*|.github/*|cmake/*|CMakeLists.txt|*/CMakeLists.txt)
                # R1: TLS bypasses
                report R1 "$f" '(curl|wget)[^#|;&]*[[:space:]](-k|--insecure|--no-check-certificate)([[:space:]]|$)' "$p" && bad=1
                report R1 "$f" 'CURLOPT_(PROXY_|DOH_)?SSL_VERIFY(PEER|HOST|STATUS)[[:space:]]*,[[:space:]]*(0|false|FALSE)' "$p" && bad=1
                report R1 "$f" 'CURLOPT_SSL_VERIFYHOST[[:space:]]*,[[:space:]]*1[^0-9]' "$p" && bad=1
                report R1 "$f" 'CURLOPT_SSL_VERIFYSTATUS' "$p" && bad=1
                ;;
        esac
        case "$f" in
            core/*|tools/*|scripts/*|.github/*|cmake/*|CMakeLists.txt|*/CMakeLists.txt)
                # R2: hard-coded SDK, toolchain and home locations
                report R2 "$f" 'pocketbook-sdk|/opt/|/home/[A-Za-z0-9._-]+/|/Users/[A-Za-z0-9._-]+/' "$p" && bad=1
                ;;
        esac
        case "$f" in
            tests/*) ;;
            *)
                # R3: private key material
                report R3 "$f" '-----BEGIN ([A-Z0-9]+ )*PRIVATE KEY-----' "$p" && bad=1
                ;;
        esac
        case "$f" in
            core/*)
                # R4: no Qt / InkView / SDK in the core
                report R4 "$f" '#[[:space:]]*include[[:space:]]*[<"](Q[A-Za-z]|inkview|ink[a-z]*\.h)|\bQObject\b|\bQString\b' "$p" && bad=1
                ;;
        esac
        case "$f" in
            .github/workflows/*.yml|.github/workflows/*.yaml)
                # R5: pinned actions
                if grep -nE '^[[:space:]-]*uses:[[:space:]]*[^.[:space:]]' "$p" | grep -vE 'uses:[[:space:]]*[A-Za-z0-9._/-]+@[0-9a-f]{40}([[:space:]]|$)' >/dev/null 2>&1; then
                    grep -nE '^[[:space:]-]*uses:[[:space:]]*[^.[:space:]]' "$p" | grep -vE 'uses:[[:space:]]*[A-Za-z0-9._/-]+@[0-9a-f]{40}([[:space:]]|$)' | sed "s#^#R5 $f:#"
                    bad=1
                fi
                # R7: no pull_request_target
                report R7 "$f" 'pull_request_target' "$p" && bad=1
                # R9: signing key never echoed or passed on a command line
                report R9 "$f" '(echo|printf|cat)[^#]*PBSTORE_SIGNING_KEY|--key[= ][^#]*(secrets\.|PBSTORE_SIGNING_KEY)|run:[^#]*\$\{\{[[:space:]]*secrets\.PBSTORE_SIGNING_KEY' "$p" && bad=1
                ;;
        esac
        case "$f" in
            .github/workflows/pull-request.yml)
                # R8: no secrets in the pull-request workflow
                report R8 "$f" 'secrets\.' "$p" && bad=1
                # R12: merged-tree and reviewer checks present
                grep -q 'check-pr-paths' "$p" || { echo "R12 $f: no check-pr-paths step"; bad=1; }
                grep -qE 'check-entry[^#]*--live-url' "$p" || { echo "R12 $f: check-entry without --live-url"; bad=1; }
                ;;
            .github/workflows/publish.yml)
                # R6: trigger, environment, concurrency, timeout
                if grep -nE '^[[:space:]]*(pull_request|pull_request_target|schedule|workflow_run):' "$p" >/dev/null 2>&1; then
                    echo "R6 $f: publish.yml may only trigger on push to main (and workflow_dispatch)"; bad=1
                fi
                grep -qE '^[[:space:]]*branches:[[:space:]]*\[[[:space:]]*main[[:space:]]*\]' "$p" || { echo "R6 $f: push trigger is not limited to branches: [main]"; bad=1; }
                grep -qE '^[[:space:]]*environment:[[:space:]]*catalog-signing' "$p" || { echo "R6 $f: no environment: catalog-signing"; bad=1; }
                grep -qE '^[[:space:]]*cancel-in-progress:[[:space:]]*false' "$p" || { echo "R6 $f: concurrency without cancel-in-progress: false"; bad=1; }
                grep -qE '^[[:space:]]*timeout-minutes:' "$p" || { echo "R6 $f: no timeout-minutes"; bad=1; }
                # R10: offline options are for tests and rehearsals only
                report R10 "$f" '(--package-dir|--now)([[:space:]=]|$)' "$p" && bad=1
                # R11: secret scan of the artifact before deployment
                if ! grep -nE 'pre-merge\.sh[[:space:]]+--secrets' "$p" >/dev/null 2>&1; then
                    echo "R11 $f: no 'pre-merge.sh --secrets' step"; bad=1
                else
                    local scan_line deploy_line
                    scan_line="$(grep -nE 'pre-merge\.sh[[:space:]]+--secrets' "$p" | head -n 1 | cut -d: -f1)"
                    deploy_line="$(grep -nE 'deploy-pages' "$p" | head -n 1 | cut -d: -f1)"
                    if [ -n "$deploy_line" ] && [ "$scan_line" -gt "$deploy_line" ]; then
                        echo "R11 $f: the secret scan runs after the Pages deployment"; bad=1
                    fi
                fi
                ;;
        esac
    done < <(list_files "$root")
    return $bad
}

if [ "${1:-}" = "--self-test" ]; then
    tmp="$(mktemp -d)"
    trap 'rm -rf "$tmp"' EXIT
    fail=0
    mkdir -p "$tmp/core/src" "$tmp/scripts" "$tmp/cmake" "$tmp/docs" "$tmp/tests" "$tmp/.github/workflows"
    expect() {  # expect <pass|fail> <description>
        scan "$tmp" >/dev/null 2>&1
        local rc=$?
        if { [ "$1" = pass ] && [ $rc -eq 0 ]; } || { [ "$1" = fail ] && [ $rc -ne 0 ]; }; then
            echo "ok   $2"
        else
            echo "FAIL $2 (rc=$rc)"; fail=1
        fi
    }
    violate() {  # violate <file> <content> <description>
        mkdir -p "$(dirname "$tmp/$1")"
        printf '%s\n' "$2" > "$tmp/$1"
        expect fail "$3"
        rm -f "$tmp/$1"
    }
    printf 'int main() { return 0; }\n' > "$tmp/core/src/ok.cpp"
    printf '#!/bin/sh\necho hello\n' > "$tmp/scripts/ok.sh"
    expect pass "a clean tree passes"
    printf 'curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);\ncurl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);\n' > "$tmp/core/src/tls_ok.cpp"
    expect pass "VERIFYPEER 1 / VERIFYHOST 2 are allowed"; rm "$tmp/core/src/tls_ok.cpp"

    violate scripts/a.sh 'curl -k https://example.com' "R1: curl -k"
    violate scripts/a.sh 'curl -sS --insecure https://example.com' "R1: curl --insecure"
    violate scripts/a.sh 'wget --no-check-certificate https://example.com' "R1: wget --no-check-certificate"
    violate core/src/a.cpp 'curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 0L);' "R1: VERIFYPEER 0"
    violate core/src/a.cpp 'curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 0L);' "R1: VERIFYHOST 0"
    violate core/src/a.cpp 'curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 1L);' "R1: VERIFYHOST 1"
    violate core/src/a.cpp 'curl_easy_setopt(c, CURLOPT_SSL_VERIFYSTATUS, 0L);' "R1: VERIFYSTATUS"
    violate core/src/a.cpp 'curl_easy_setopt(c, CURLOPT_PROXY_SSL_VERIFYPEER, 0L);' "R1: PROXY_SSL_VERIFYPEER 0"
    violate core/src/a.cpp 'curl_easy_setopt(c, CURLOPT_DOH_SSL_VERIFYHOST, false);' "R1: DOH_SSL_VERIFYHOST false"
    violate .github/workflows/x.yml '      - run: curl -k https://example.com' "R1: curl -k in a workflow"
    violate cmake/a.cmake 'set(SDK_PATH /opt/pocketbook-sdk/x)' "R2: /opt/pocketbook path"
    violate scripts/a.sh 'SDK=../pocketbook-sdk-6.11/sdk' "R2: pocketbook-sdk path"
    violate scripts/a.sh 'TOOLCHAIN=/opt/toolchain/bin' "R2: /opt/ toolchain path"
    violate core/src/a.cpp 'const char* p = "/home/someone/data";' "R2: home path in a source"
    violate CMakeLists.txt 'set(X /Users/someone/x)' "R2: macOS home path in CMake"
    violate docs/key.txt '-----BEGIN PRIVATE KEY-----' "R3: private key in docs"
    violate core/src/a.cpp '// -----BEGIN OPENSSH PRIVATE KEY-----' "R3: private key in core"
    printf -- '-----BEGIN PRIVATE KEY-----\n' > "$tmp/tests/key.txt"
    expect pass "R3: private key text inside tests/ is allowed"; rm "$tmp/tests/key.txt"
    violate core/src/a.cpp '#include <QString>' "R4: Qt include in core"
    violate core/src/a.cpp '#include <inkview.h>' "R4: inkview include in core"
    pinned='      - uses: actions/checkout@0123456789abcdef0123456789abcdef01234567 # v4'
    printf 'name: x\njobs:\n  a:\n    steps:\n%s\n      - uses: ./.github/actions/local\n' "$pinned" > "$tmp/.github/workflows/ok.yml"
    expect pass "R5: pinned and local actions are allowed"; rm "$tmp/.github/workflows/ok.yml"
    violate .github/workflows/x.yml '      - uses: actions/checkout@v4' "R5: action pinned to a tag"
    violate .github/workflows/x.yml '      - uses: actions/checkout@main' "R5: action pinned to a branch"
    violate .github/workflows/x.yml 'on: pull_request_target' "R7: pull_request_target"
    violate .github/workflows/x.yml '      - run: echo "$PBSTORE_SIGNING_KEY"' "R9: echoing the signing key"
    violate .github/workflows/x.yml '      - run: pbstore-catalog sign --key ${{ secrets.PBSTORE_SIGNING_KEY }}' "R9: key on a command line"

    # pull-request.yml
    printf 'on: pull_request\njobs:\n  a:\n    steps:\n      - run: pbstore-catalog check-pr-paths\n      - run: pbstore-catalog check-entry x --live-url u --keys k\n' > "$tmp/.github/workflows/pull-request.yml"
    expect pass "R8/R12: a good pull-request.yml passes"
    printf 'env:\n  K: ${{ secrets.X }}\n' >> "$tmp/.github/workflows/pull-request.yml"
    expect fail "R8: secrets in pull-request.yml"
    printf 'on: pull_request\njobs:\n  a:\n    steps:\n      - run: pbstore-catalog check-entry x --live-url u --keys k\n' > "$tmp/.github/workflows/pull-request.yml"
    expect fail "R12: no check-pr-paths step"
    printf 'on: pull_request\njobs:\n  a:\n    steps:\n      - run: pbstore-catalog check-pr-paths\n      - run: pbstore-catalog check-entry x --keys k\n' > "$tmp/.github/workflows/pull-request.yml"
    expect fail "R12: check-entry without --live-url"
    rm "$tmp/.github/workflows/pull-request.yml"

    # publish.yml
    good_publish() {
        cat <<'YAML'
on:
  push:
    branches: [main]
  workflow_dispatch:
concurrency:
  group: publish
  cancel-in-progress: false
jobs:
  publish:
    environment: catalog-signing
    timeout-minutes: 20
    steps:
      - run: pbstore-catalog build --entries catalog/entries --out out --live-url u --keys k
      - run: scripts/pre-merge.sh --secrets out
      - uses: actions/deploy-pages@0123456789abcdef0123456789abcdef01234567 # v4
YAML
    }
    good_publish > "$tmp/.github/workflows/publish.yml"
    expect pass "R6/R10/R11: a good publish.yml passes"
    good_publish | sed 's/branches: \[main\]/branches: [main, dev]/' > "$tmp/.github/workflows/publish.yml"
    expect fail "R6: push trigger not limited to main"
    good_publish | sed 's/^on:/on:\n  pull_request:/' > "$tmp/.github/workflows/publish.yml"
    expect fail "R6: pull_request trigger in publish.yml"
    good_publish | grep -v 'environment: catalog-signing' > "$tmp/.github/workflows/publish.yml"
    expect fail "R6: no signing environment"
    good_publish | sed 's/cancel-in-progress: false/cancel-in-progress: true/' > "$tmp/.github/workflows/publish.yml"
    expect fail "R6: cancel-in-progress true"
    good_publish | grep -v 'timeout-minutes' > "$tmp/.github/workflows/publish.yml"
    expect fail "R6: no timeout"
    good_publish | sed 's#--keys k#--keys k --package-dir x#' > "$tmp/.github/workflows/publish.yml"
    expect fail "R10: --package-dir in publish.yml"
    good_publish | sed 's#--keys k#--keys k --now 5#' > "$tmp/.github/workflows/publish.yml"
    expect fail "R10: --now in publish.yml"
    good_publish | grep -v 'pre-merge.sh --secrets' > "$tmp/.github/workflows/publish.yml"
    expect fail "R11: no secret scan"
    good_publish | awk '/deploy-pages/ {print; print "      - run: scripts/pre-merge.sh --secrets out"; next} /pre-merge.sh --secrets/ {next} {print}' > "$tmp/.github/workflows/publish.yml"
    expect fail "R11: secret scan after the deployment"
    exit $fail
fi

ROOT="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
if scan "$ROOT"; then
    echo "constitution checks passed"
else
    echo "constitution checks FAILED (see findings above)" >&2
    exit 1
fi
