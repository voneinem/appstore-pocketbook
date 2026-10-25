# Mozilla CA bundle (trust-anchor fallback)

`cacert.pem` is the CA certificate bundle that the curl project extracts from Mozilla's certificate
store (https://curl.se/docs/caextract.html). The installer uses it **only** when none of the usual
system bundles can be found (`core/install/src/trust_anchors.cpp`); it never weakens verification.

- Licence of the data: MPL-2.0 (Mozilla Public License 2.0, https://www.mozilla.org/MPL/2.0/).
- `VERSION` records the date, the source address and the SHA-256 of the file as published by curl.

## How to update

By a reviewed change, never automatically:

1. Download `https://curl.se/ca/cacert.pem` and `https://curl.se/ca/cacert.pem.sha256`.
2. Check that `sha256sum cacert.pem` equals the published value.
3. Replace `cacert.pem`, update `VERSION` (date from the first lines of the file, address, SHA-256).
4. Build and run the tests; the CMake step refuses a file whose SHA-256 differs from `VERSION`.
