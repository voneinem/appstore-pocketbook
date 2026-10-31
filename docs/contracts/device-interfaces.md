# Contract: The Device Interfaces of the Store

**Version**: 1.0 · **Status**: draft for implementation · Governed by constitution 2.1.0 Principle VI. Each interface is a
Qt-free, SDK-free header in `core/store/`; the reader implementation is the only code that includes `inkview.h`
(`platform/pocketbook/`); the tests use fakes. Reasoning and device facts: [../research.md](../research.md) R-03, R-06, R-07, R-13.

## The two sets

1. **Interfaces of feature 002** (`ReaderFs`, `StateDirs`, `Clock`, `MountSource`, `OpenUrl`, `IdentitySource`): reader
   implementations are listed in research R-03 (`PosixReaderFs` and `PosixStateDirs` over `/mnt/ext1`, `SystemClock`,
   `FixedMountSource`, none for `OpenUrl`, `InkViewIdentity`). The same contract tests run against the computer and the reader
   configurations; on the host both are the same classes over a temporary folder, over `FatRulesFs` and over `CrashFs`.
2. **New interfaces of the store** (below).

## `Radio` (Wi-Fi)

```cpp
class Radio {
public:
    virtual ~Radio() = default;
    virtual std::optional<bool> is_on() = 0;          // radio powered (nullopt: cannot be read)
    virtual bool is_connected() = 0;                  // associated and online (QueryNetwork() & NET_CONNECTED)
    virtual bool connect() = 0;                       // start a connection without waiting; false if refused at once
    virtual bool disconnect() = 0;                    // switch off what connect() switched on
    virtual int last_error() = 0;                     // 0 or a platform code; never shown raw
};
```

Reader implementation: `QueryNetwork`, `GetWiFiPowerStatus`, `NetConnectAsync`/`NetConnectSilent`, `NetDisconnect`,
`GetLastNetConnectionError`. `SetFlightMode` is not used. Semantics in research R-06; `RadioSession` is the only caller.

## `Battery`

```cpp
class Battery {
public:
    virtual ~Battery() = default;
    virtual std::optional<int> percent() = 0;         // 0..100, nullopt if unreadable
    virtual std::optional<bool> charging() = 0;
};
```

Reader: `GetBatteryPower`, `IsCharging`. `PowerGate` is the only caller (thresholds in research R-07).

## `Screen`

```cpp
class Screen {
public:
    virtual ~Screen() = default;
    virtual void full_update() = 0;                   // FullUpdate()
};
```

`RefreshPolicy` decides when to call it (research R-13). Nothing else about the panel is controlled by the store.

## `MemoryInfo`

```cpp
class MemoryInfo {
public:
    virtual ~MemoryInfo() = default;
    virtual std::optional<uint64_t> available_bytes() = 0;   // MemAvailable from /proc/meminfo
};
```

Reader and host implementation read `/proc/meminfo` (POSIX, in `platform/` and in a host fake). `MemoryGate` is the only caller.

## `Exit` hooks

The app registers one function with `Radio` cleanup: `RadioSession::end()` runs on a normal leave, on `EVT_EXIT`, and when the
operation's worker ends by an exception. A hard kill cannot run it (residual risk, plan risk table).

## Fakes (in `tests/support/`)

`FakeRadio` (scriptable: starts on/off, connect succeeds/fails/times out, disconnect succeeds/fails, records every call),
`FakeBattery`, `FakeScreen` (counts calls), `FakeMemory`. Each fake is itself covered by a test so the contract tests cannot
pass vacuously.

## Contract tests (`tests/store/test_device_interfaces`)

For every interface, the same cases run against the fake and — where a host implementation exists (`MemoryInfo`, the
filesystem ones) — against that: a `Radio` fake obeys the call-order rules `RadioSession` relies on; a refused `connect()`
reports `false`; `disconnect()` is idempotent; `percent()` stays in 0..100 or is empty.
