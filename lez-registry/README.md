# LEZ Registry — Logos Blog On-Chain Author Index

A [SPEL](https://github.com/logos-co/spel) program that stores an
`author_pubkey → Vec<CID>` mapping on the Logos Execution Zone (LEZ) ledger.

Blog posts are stored in the Logos Storage module as content-addressed blobs.
The LEZ registry is the on-chain index that lets any node enumerate an author's
published posts without trusting a centralised server.

## Architecture

```
BlogPlugin (C++)
  │
  ├─ publish post ──→ StorageSync ──uploadUrl()──→ Logos Storage module
  │                                                 └── returns CID
  │
  └─ register CID ──→ LEZ Registry (this program)
                        instruction: register_post(author, cid)
```

Subscribers call `get_posts(author_pubkey)` to discover all CIDs for an author,
then fetch each post body from the Storage module via `downloadChunks(cid)`.

## Program Instructions

| Instruction | Signer | Description |
|-------------|--------|-------------|
| `initialize` | author | Create the registry account for a new author |
| `register_post(author, cid)` | author | Append a CID to the author's post list |
| `remove_post(author, cid)` | author | Remove a CID (e.g. after post deletion) |
| `get_posts(author)` | none (read-only) | Return all CIDs for an author |

## Account Layout

Each author has one `Registry` account (PDA derived from their Ed25519 pubkey):

```rust
struct Registry {
    author_pubkey: String,  // 64-char hex Ed25519 pubkey
    cids: Vec<String>,      // ordered list of storage CIDs (oldest first)
    version: u8,            // schema version (currently 1)
}
```

## Building

### With the SPEL runtime (on-chain build)

> **Note:** SPEL is experimental. Install the SPEL toolchain from
> https://github.com/logos-co/spel before building.

```bash
# Build the RISC Zero guest binary for LEZ deployment
make build

# Deploy to LEZ devnet (update program-id as appropriate)
lez-cli deploy target/riscv-guest/riscv32im-risc0-zkvm-elf/release/blog_registry.bin --network devnet
```

### Local / CI (no SPEL runtime)

```bash
# Compiles and runs unit tests using the in-memory mock implementation
cargo test
```

## Usage from the Blog Plugin (C++)

The blog plugin calls the LEZ registry indirectly via `LogosAPIClient::invokeRemoteMethod`.
A future integration (Phase N) will wire `BlogPlugin::publishPost` to also call
`register_post` on the LEZ registry after a successful storage upload:

```cpp
// Pseudocode — see src/blog_plugin.cpp for the actual integration point
if (m_lez && !cid.isEmpty()) {
    m_lez->invokeRemoteMethod("lez_module", "callProgram",
        "lez_registry", "register_post",
        QJsonObject{{"author", m_ownPubkey}, {"cid", cid}});
}
```

## Known Limitations

### `get_posts` does not return a value directly

SPEL has no stable read-only / view-function convention yet (no `#[query]`
attribute or `LezOutput::return_value` API). The `get_posts` instruction
currently validates the account and echoes both accounts back unchanged.

To read an author's CID list, callers must fetch the registry account data
out-of-band (e.g. via `getAccountData(pda)`) and deserialise it as a `Registry`
struct using Borsh. Once SPEL stabilises a view-function mechanism this
instruction will be updated to return `Vec<String>` directly.

## SPEL Compatibility Notes

SPEL is under active development. The following items may need updating when
a stable release is published:

- Proc-macro names (`#[lez_program]`, `#[instruction]`, `#[derive(Accounts)]`)
- Account context types (`Context<T>`, `Account<'info, T>`)
- Error type (`ProgramError` variants)
- Serialisation derive macros (`BorshSerialize`, `BorshDeserialize`)
- The `get_posts` return-value convention (some frameworks use a "view" type)
- PDA derivation API

All SPEL-specific code is gated behind the `on-chain` feature flag so the crate
remains compilable for local testing without the SPEL toolchain.

## License

Part of the Logos Blog project. See repository root for license information.
