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
#   R13 PBSTORE_TEST_* names and any catalog-address, key or CA override appear only under
#        "#ifdef PBSTORE_TEST_BUILD" and in tests/ (the released installer has no override).
#   R14 POSIX headers (<unistd.h>, <fcntl.h>, <signal.h>, <sys/...>) in core/install/ and tools/ only
#        in core/install/src/posix_*.cpp, tools/pbstore-install-ui/{http_server,open_url_xdg}.cpp and
#        tools/pbstore-install/{signals,prompts,progress_terminal}.cpp.
#   R15 The assistant (tools/pbstore-install-ui/) never binds INADDR_ANY, "0.0.0.0" or "::".
#   R16 No system( or popen( in core/install/ and tools/pbstore-install*.
#   R17 release-installer.yml: triggers only on tags v*, "contents: write" only on the release job,
#        no secrets, container image pinned by @sha256: digest, a step creating SHA256SUMS.
# The store app on the reader (feature 003) adds these, and extends R1, R2, R14 and R16 to app/, bridge/,
# platform/, packaging/ and core/store/:
#   R18 inkview.h and Qt Quick/QML includes (QQuick*, QtQuick*, QtQml*, QQml*) only under platform/pocketbook/,
#        app/, bridge/, qml/ and tests/ (the host shim and the Qt tests); never in core/ or tools/.
#   R19 The names of the install protocol (10-intent, 20-prepared, 30-commit, the journal name constants, the
#        view.json editing functions) do not appear in app/, bridge/, platform/ or core/store/: the store has
#        no second copy of the install logic (constitution Principle VI).
#   R20 PBSTORE_TEST_* names appear in app/, bridge/, platform/ and core/store/ only inside
#        "#ifdef PBSTORE_TEST_BUILD", and PBSTORE_DEV_* names only inside "#ifdef PBSTORE_DEV_BUILD".
#   R14 (extended) POSIX headers in core/store/, bridge/ and app/ only in app/device_bridge.cpp and
#        core/store/src/posix_*.cpp; platform/pocketbook/ is where the reader implementations live and is free.
#   R16 (extended) no system( or popen( in core/store/, app/, bridge/ and platform/.
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

# ifdef_violations <file> <switch> <name-regex>: print the lines that name <name-regex> outside an
# "#ifdef <switch>" (or "#if defined(<switch>)") block; preprocessor lines themselves are not names.
ifdef_violations() {
    awk -v sw="$2" -v name="$3" '
        $0 ~ "^[[:space:]]*#[[:space:]]*(ifdef[[:space:]]+" sw "|if[[:space:]]+defined\\(" sw "\\))" { depth=1; next }
        depth>0 && /^[[:space:]]*#[[:space:]]*if/ { depth++ }
        depth>0 && /^[[:space:]]*#[[:space:]]*endif/ { depth--; next }
        depth>0 && /^[[:space:]]*#[[:space:]]*else/ && depth==1 { depth=0; next }
        depth==0 && $0 ~ name && !/^[[:space:]]*#/ { print FILENAME ":" NR ": " $0 }' "$1"
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
            core/*|tools/*|scripts/*|tests/*|.github/*|cmake/*|app/*|bridge/*|platform/*|packaging/*|CMakeLists.txt|*/CMakeLists.txt)
                # R1: TLS bypasses
                report R1 "$f" '(curl|wget)[^#|;&]*[[:space:]](-k|--insecure|--no-check-certificate)([[:space:]]|$)' "$p" && bad=1
                report R1 "$f" 'CURLOPT_(PROXY_|DOH_)?SSL_VERIFY(PEER|HOST|STATUS)[[:space:]]*,[[:space:]]*(0|false|FALSE)' "$p" && bad=1
                report R1 "$f" 'CURLOPT_SSL_VERIFYHOST[[:space:]]*,[[:space:]]*1[^0-9]' "$p" && bad=1
                report R1 "$f" 'CURLOPT_SSL_VERIFYSTATUS' "$p" && bad=1
                ;;
        esac
        case "$f" in
            core/*|tools/*|scripts/*|.github/*|cmake/*|app/*|bridge/*|platform/*|packaging/*|CMakeLists.txt|*/CMakeLists.txt)
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
            core/install/*|tools/pbstore-install*)
                # R13: test-only names only under #ifdef PBSTORE_TEST_BUILD
                if grep -nE 'PBSTORE_TEST_(CATALOG_URL|CA_FILE|STATE_DIR|KEY|[A-Z_]+)' "$p" >/dev/null 2>&1; then
                    case "$f" in
                        *CMakeLists.txt|*.cmake) ;;
                        *)
                            if ! awk '
                                /^[[:space:]]*#[[:space:]]*(ifdef[[:space:]]+PBSTORE_TEST_BUILD|if[[:space:]]+defined\(PBSTORE_TEST_BUILD\))/ { depth=1; next }
                                depth>0 && /^[[:space:]]*#[[:space:]]*if/ { depth++ }
                                depth>0 && /^[[:space:]]*#[[:space:]]*endif/ { depth--; next }
                                depth>0 && /^[[:space:]]*#[[:space:]]*else/ && depth==1 { depth=0; next }
                                depth==0 && /PBSTORE_TEST_(CATALOG_URL|CA_FILE|STATE_DIR|KEY|[A-Z_]+)/ && !/PBSTORE_TEST_BUILD/ { print FILENAME ":" NR ": " $0; bad=1 }
                                END { exit bad }' "$p" > "$p.r13.$$" 2>/dev/null; then
                                sed "s#^#R13 $f:#" "$p.r13.$$"; bad=1
                            fi
                            rm -f "$p.r13.$$"
                            ;;
                    esac
                fi
                # R14: POSIX headers only in the posix_*.cpp files and a few named tool files
                case "$f" in
                    core/install/src/posix_*.cpp|tools/pbstore-install-ui/http_server.cpp|tools/pbstore-install-ui/open_url_xdg.cpp|tools/pbstore-install/signals.cpp|tools/pbstore-install/prompts.cpp|tools/pbstore-install/progress_terminal.cpp) ;;
                    *.cpp|*.h|*.hpp)
                        report R14 "$f" '#[[:space:]]*include[[:space:]]*<(unistd|fcntl|signal|poll|dirent|termios|pthread|netdb)\.h>|#[[:space:]]*include[[:space:]]*<(sys|netinet|arpa)/' "$p" && bad=1
                        ;;
                esac
                # R16: no shell
                case "$f" in
                    *.cpp|*.h|*.hpp)
                        report R16 "$f" '(^|[^A-Za-z0-9_.>:])(system|popen)[[:space:]]*\(' "$p" && bad=1
                        ;;
                esac
                ;;
        esac
        case "$f" in
            app/*|bridge/*|platform/*|core/store/*)
                case "$f" in
                    *.cpp|*.h|*.hpp|*.c)
                        # R20: test-only and dev-only names only under their switches
                        ifdef_violations "$p" PBSTORE_TEST_BUILD 'PBSTORE_TEST_[A-Z_]+' > "$p.r20.$$"
                        ifdef_violations "$p" PBSTORE_DEV_BUILD 'PBSTORE_DEV_[A-Z_]+' >> "$p.r20.$$"
                        if [ -s "$p.r20.$$" ]; then sed "s#^#R20 $f:#" "$p.r20.$$"; bad=1; fi
                        rm -f "$p.r20.$$"
                        # R16 (extended): no shell
                        report R16 "$f" '(^|[^A-Za-z0-9_.>:])(system|popen)[[:space:]]*\(' "$p" && bad=1
                        ;;
                esac
                # R19: no second copy of the install protocol
                report R19 "$f" '10-intent|20-prepared|30-commit|\b(parse_desktop_config|replace_entry|remove_entry|entry_value_from_manifest|kIntentName|kPreparedName|kCommitName)\b' "$p" && bad=1
                ;;
        esac
        case "$f" in
            core/store/*|bridge/*|app/*)
                # R14 (extended): POSIX headers only in the named files
                case "$f" in
                    app/device_bridge.cpp|core/store/src/posix_*.cpp) ;;
                    *.cpp|*.h|*.hpp)
                        report R14 "$f" '#[[:space:]]*include[[:space:]]*<(unistd|fcntl|signal|poll|dirent|termios|pthread|netdb)\.h>|#[[:space:]]*include[[:space:]]*<(sys|netinet|arpa)/' "$p" && bad=1
                        ;;
                esac
                ;;
        esac
        case "$f" in
            platform/pocketbook/*|app/*|bridge/*|qml/*|tests/*) ;;
            *.cpp|*.h|*.hpp|*.c)
                # R18: InkView and Qt Quick/QML only where the reader's interface lives
                report R18 "$f" '#[[:space:]]*include[[:space:]]*[<"]([^>"]*/)?(inkview\.h|QQuick[A-Za-z]*|QtQuick[A-Za-z]*/|QtQml[A-Za-z]*/|QQml[A-Za-z]*)' "$p" && bad=1
                ;;
        esac
        case "$f" in
            tools/pbstore-install-ui/*)
                # R15: loopback only
                report R15 "$f" 'INADDR_ANY|"0\.0\.0\.0"|"::"|in6addr_any' "$p" && bad=1
                ;;
        esac
        case "$f" in
            .github/workflows/release-installer.yml)
                # R17: release workflow shape
                if grep -nE '^[[:space:]]*(pull_request|pull_request_target|schedule|workflow_run|workflow_dispatch):' "$p" >/dev/null 2>&1 \
                   || grep -nE '^[[:space:]]*branches:' "$p" >/dev/null 2>&1; then
                    echo "R17 $f: the release workflow may only trigger on tags v*"; bad=1
                fi
                grep -qE "^[[:space:]]*tags:[[:space:]]*\[[[:space:]]*['\"]?v\*['\"]?[[:space:]]*\]" "$p" || { echo "R17 $f: no tags: ['v*'] trigger"; bad=1; }
                report R17 "$f" 'secrets\.' "$p" && bad=1
                # contents: write only on the release job (everything at the top level must be read-only)
                if ! awk '
                    /^permissions:/ { top=1; next }
                    top && /^[^[:space:]]/ { top=0 }
                    top && /contents:[[:space:]]*write/ { bad=1 }
                    END { exit bad }' "$p"; then
                    echo "R17 $f: top-level permissions must not grant contents: write"; bad=1
                fi
                grep -qE '^[[:space:]]*contents:[[:space:]]*write' "$p" || { echo "R17 $f: the release job has no contents: write"; bad=1; }
                if [ "$(grep -cE 'contents:[[:space:]]*write' "$p")" -gt 1 ]; then echo "R17 $f: contents: write more than once"; bad=1; fi
                grep -qE '@sha256:[0-9a-f]{64}' "$p" || { echo "R17 $f: container image not pinned by @sha256: digest"; bad=1; }
                grep -qE 'SHA256SUMS' "$p" || { echo "R17 $f: no step creating SHA256SUMS"; bad=1; }
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
    # R13 .. R17 (installer)
    rm -f "$tmp/.github/workflows/publish.yml"
    mkdir -p "$tmp/core/install/src" "$tmp/tools/pbstore-install" "$tmp/tools/pbstore-install-ui"
    printf '#include <string>\nint f();\n' > "$tmp/core/install/src/planner.cpp"
    expect pass "R14: a planner without POSIX headers passes"
    violate core/install/src/planner.cpp '#include <unistd.h>' "R14: unistd.h in core/install/src/planner.cpp"
    violate core/install/src/planner.cpp '#include <sys/stat.h>' "R14: sys/stat.h in the planner"
    violate tools/pbstore-install/args.cpp '#include <signal.h>' "R14: signal.h in tools/pbstore-install/args.cpp"
    printf '#include <unistd.h>\n#include <fcntl.h>\n' > "$tmp/core/install/src/posix_reader_fs.cpp"
    expect pass "R14: POSIX headers are allowed in posix_*.cpp"; rm "$tmp/core/install/src/posix_reader_fs.cpp"
    printf '#include <sys/socket.h>\n#include <netinet/in.h>\n' > "$tmp/tools/pbstore-install-ui/http_server.cpp"
    expect pass "R14: POSIX headers are allowed in http_server.cpp"; rm "$tmp/tools/pbstore-install-ui/http_server.cpp"
    printf '#include <termios.h>\n#include <unistd.h>\n' > "$tmp/tools/pbstore-install/prompts.cpp"
    expect pass "R14: POSIX headers are allowed in prompts.cpp"; rm "$tmp/tools/pbstore-install/prompts.cpp"
    violate tools/pbstore-install-ui/api.cpp '#include <poll.h>' "R14: poll.h in api.cpp"

    printf 'const char* u = "x";\n#ifdef PBSTORE_TEST_BUILD\nconst char* t = getenv("PBSTORE_TEST_CATALOG_URL");\n#endif\n' > "$tmp/core/install/src/build_config.cpp"
    expect pass "R13: test names inside #ifdef PBSTORE_TEST_BUILD pass"
    printf '#ifdef PBSTORE_TEST_BUILD\n#if 1\nint a;\n#endif\nconst char* t = getenv("PBSTORE_TEST_CA_FILE");\n#endif\n' > "$tmp/core/install/src/build_config.cpp"
    expect pass "R13: nested conditionals inside the test block pass"
    rm "$tmp/core/install/src/build_config.cpp"
    violate core/install/src/build_config.cpp 'const char* t = getenv("PBSTORE_TEST_CATALOG_URL");' "R13: test name outside the ifdef"
    violate tools/pbstore-install/main.cpp 'auto s = std::getenv("PBSTORE_TEST_STATE_DIR");' "R13: PBSTORE_TEST_STATE_DIR in main.cpp"
    violate core/install/src/build_config.cpp '#ifdef PBSTORE_TEST_BUILD
int a;
#else
const char* t = getenv("PBSTORE_TEST_CA_FILE");
#endif' "R13: test name in the #else branch"

    violate core/install/src/planner.cpp 'int r = system("ls");' "R16: system( in core/install"
    violate tools/pbstore-install/main.cpp 'FILE* p = popen("ls", "r");' "R16: popen( in tools/pbstore-install"
    printf 'int a = fsystem(1);\nbool b = fs.system_ok();\n' > "$tmp/core/install/src/ok16.cpp"
    expect pass "R16: names that merely contain system pass"; rm "$tmp/core/install/src/ok16.cpp"

    violate tools/pbstore-install-ui/http_server.cpp 'addr.sin_addr.s_addr = htonl(INADDR_ANY);' "R15: INADDR_ANY"
    violate tools/pbstore-install-ui/http_server.cpp 'inet_pton(AF_INET, "0.0.0.0", &a);' "R15: 0.0.0.0"
    violate tools/pbstore-install-ui/http_server.cpp 'inet_pton(AF_INET6, "::", &a);' "R15: ::"
    printf 'inet_pton(AF_INET, "127.0.0.1", &a);\n' > "$tmp/tools/pbstore-install-ui/http_server.cpp"
    expect pass "R15: 127.0.0.1 passes"; rm "$tmp/tools/pbstore-install-ui/http_server.cpp"

    good_release() {
        cat <<'YAML'
on:
  push:
    tags: ['v*']
permissions:
  contents: read
jobs:
  release:
    permissions:
      contents: write
    container: docker.io/library/alpine@sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef
    steps:
      - uses: actions/checkout@0123456789abcdef0123456789abcdef01234567 # v7
      - run: sha256sum pbstore-install-1 > SHA256SUMS
YAML
    }
    good_release > "$tmp/.github/workflows/release-installer.yml"
    expect pass "R17: a good release-installer.yml passes"
    good_release | sed "s/tags: \['v\*'\]/branches: [main]/" > "$tmp/.github/workflows/release-installer.yml"
    expect fail "R17: branch trigger"
    good_release | sed 's/^on:/on:\n  workflow_dispatch:/' > "$tmp/.github/workflows/release-installer.yml"
    expect fail "R17: workflow_dispatch trigger"
    good_release | sed 's/contents: read/contents: write/' > "$tmp/.github/workflows/release-installer.yml"
    expect fail "R17: contents: write at the top level"
    good_release | grep -v 'contents: write' > "$tmp/.github/workflows/release-installer.yml"
    expect fail "R17: release job without contents: write"
    good_release | sed 's/$/\n/;s/steps:/env:\n      T: ${{ secrets.X }}\n    steps:/' > "$tmp/.github/workflows/release-installer.yml"
    expect fail "R17: secrets"
    good_release | sed 's/@sha256:[0-9a-f]*/:latest/' > "$tmp/.github/workflows/release-installer.yml"
    expect fail "R17: image without digest"
    good_release | grep -v SHA256SUMS > "$tmp/.github/workflows/release-installer.yml"
    expect fail "R17: no SHA256SUMS step"
    rm -f "$tmp/.github/workflows/release-installer.yml"

    # --- the store app on the reader (R18 to R20 and the extensions of R1, R2, R14, R16) ---
    mkdir -p "$tmp/core/store/src" "$tmp/app" "$tmp/bridge" "$tmp/qml" "$tmp/platform/pocketbook" "$tmp/tests/support/inkview_shim" "$tmp/tests/bridge"
    # R18: InkView and Qt Quick only in platform/pocketbook, app, bridge, qml and tests
    violate core/store/src/a.cpp '#include <QQuickItem>' "R18: Qt Quick include in core/store"
    violate tools/pbstore-install/a.cpp '#include <inkview.h>' "R18: inkview include in tools"
    violate tools/pbstore-install/b.cpp '#include <QtQuick/QQuickView>' "R18: Qt Quick module include in tools"
    printf '#include <inkview.h>\n' > "$tmp/platform/pocketbook/radio.cpp"
    expect pass "R18: inkview.h is allowed under platform/pocketbook"; rm "$tmp/platform/pocketbook/radio.cpp"
    printf '#include <QQuickView>\n#include <QObject>\n' > "$tmp/app/main.cpp"
    expect pass "R18: Qt Quick is allowed under app"; rm "$tmp/app/main.cpp"
    printf '#include <QObject>\n#include <QQmlEngine>\n' > "$tmp/bridge/controller.cpp"
    expect pass "R18: Qt is allowed under bridge"; rm "$tmp/bridge/controller.cpp"
    printf 'void FullUpdate();\n#include <inkview.h>\n' > "$tmp/tests/support/inkview_shim/inkview.h"
    expect pass "R18: the host shim under tests is allowed"; rm "$tmp/tests/support/inkview_shim/inkview.h"
    printf '#include <QtTest>\n' > "$tmp/tests/bridge/test_x.cpp"
    expect pass "R18: Qt Test under tests is allowed"; rm "$tmp/tests/bridge/test_x.cpp"
    # R19: no copy of the install protocol in the store
    violate app/a.cpp 'const char* m = "30-commit";' "R19: commit marker name in app"
    violate bridge/a.cpp 'auto c = parse_desktop_config(text);' "R19: view.json parsing in bridge"
    violate platform/pocketbook/a.cpp 'const char* n = "10-intent.json";' "R19: intent file name in platform"
    violate core/store/src/a.cpp 'const char* n = "20-prepared";' "R19: prepared marker name in core/store"
    violate core/store/src/b.cpp 'auto c = replace_entry(id, v, g);' "R19: view.json edit in core/store"
    printf 'constexpr const char* kCommitName = "30-commit";\n' > "$tmp/core/install/src/journal_names.cpp"
    expect pass "R19: the engine itself may name its protocol"; rm "$tmp/core/install/src/journal_names.cpp"
    # R20: test-only and dev-only names under their switches
    violate app/a.cpp 'const char* t = getenv("PBSTORE_TEST_CATALOG_URL");' "R20: test name outside the ifdef in app"
    violate bridge/a.cpp 'auto s = std::getenv("PBSTORE_TEST_STATE_DIR");' "R20: test name in bridge"
    violate platform/pocketbook/a.cpp 'const char* t = "PBSTORE_TEST_KEY";' "R20: test name in platform"
    violate core/store/src/a.cpp 'const char* t = getenv("PBSTORE_TEST_CA_FILE");' "R20: test name in core/store"
    printf '#ifdef PBSTORE_TEST_BUILD\nconst char* t = getenv("PBSTORE_TEST_CATALOG_URL");\n#endif\n' > "$tmp/app/store_config.cpp"
    expect pass "R20: test names inside #ifdef PBSTORE_TEST_BUILD pass in app"; rm "$tmp/app/store_config.cpp"
    violate app/a.cpp 'const char* m = PBSTORE_DEV_BUILD_MARKER;' "R20: dev marker outside the dev switch"
    violate app/a.cpp '#ifdef PBSTORE_TEST_BUILD
const char* m = PBSTORE_DEV_BUILD_MARKER;
#endif' "R20: dev marker under the test switch only"
    printf '#ifdef PBSTORE_DEV_BUILD\nconst char* m = PBSTORE_DEV_BUILD_MARKER;\n#endif\n' > "$tmp/app/dev.cpp"
    expect pass "R20: the dev marker inside #ifdef PBSTORE_DEV_BUILD passes"; rm "$tmp/app/dev.cpp"
    # R14 extension: POSIX headers only in the reader implementations
    violate core/store/src/a.cpp '#include <unistd.h>' "R14: unistd.h in core/store"
    violate bridge/a.cpp '#include <sys/stat.h>' "R14: sys/stat.h in bridge"
    violate app/main.cpp '#include <signal.h>' "R14: signal.h in app/main.cpp"
    printf '#include <unistd.h>\n#include <signal.h>\n' > "$tmp/app/device_bridge.cpp"
    expect pass "R14: POSIX headers are allowed in app/device_bridge.cpp"; rm "$tmp/app/device_bridge.cpp"
    printf '#include <unistd.h>\n#include <sys/stat.h>\n' > "$tmp/platform/pocketbook/probe.cpp"
    expect pass "R14: POSIX headers are allowed under platform/pocketbook"; rm "$tmp/platform/pocketbook/probe.cpp"
    # R16 extension: no shell in the store
    violate core/store/src/a.cpp 'int r = system("ls");' "R16: system( in core/store"
    violate platform/pocketbook/a.cpp 'FILE* p = popen("ls", "r");' "R16: popen( in platform"
    violate app/a.cpp 'int r = system("sync");' "R16: system( in app"
    # R1 and R2 extension
    violate platform/pocketbook/a.cpp 'curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 0L);' "R1: VERIFYPEER 0 in platform"
    violate app/a.cpp 'curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 0L);' "R1: VERIFYHOST 0 in app"
    violate app/a.cpp 'const char* sdk = "/opt/pocketbook-sdk/x";' "R2: SDK path in app"
    violate platform/pocketbook/pbstore.app 'sh /home/someone/x' "R2: home path in the launcher"
    violate packaging/manifest.template.json '{"path": "/Users/someone/x"}' "R2: home path in packaging"
    exit $fail
fi

ROOT="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
if scan "$ROOT"; then
    echo "constitution checks passed"
else
    echo "constitution checks FAILED (see findings above)" >&2
    exit 1
fi
