//! # LEZ Registry — Logos Blog Author→CID On-Chain Index
//!
//! A SPEL program that stores a mapping of `author_pubkey → Vec<CID>` on the
//! Logos Execution Zone (LEZ) ledger. Blog posts are published to the Logos
//! Storage module; their CIDs are registered here so any node can enumerate
//! an author's posts without relying on a centralised index.
//!
//! ## Instructions
//!
//! | Instruction | Description |
//! |-------------|-------------|
//! | `initialize` | Create the registry account for the caller |
//! | `register_post` | Append a CID to an author's post list |
//! | `remove_post` | Remove a CID from an author's post list |
//! | `get_posts` | Query all CIDs for a given author pubkey |
//!
//! ## SPEL compatibility note
//!
//! SPEL is experimental. The proc-macro names (`#[lez_program]`, `#[instruction]`)
//! and the account/context types may change. All SPEL-specific items are gated
//! behind the `on-chain` feature so the crate can be type-checked locally without
//! the SPEL runtime installed.
//!
//! TODO: Audit against SPEL API once a stable release is published.

// ── Conditional SPEL imports ──────────────────────────────────────────────────

#[cfg(feature = "on-chain")]
use spel::{
    account::Account,
    context::Context,
    error::ProgramError,
    instruction,
    lez_program,
    pubkey::Pubkey,
};

// ── Data types (shared between on-chain and mock) ──────────────────────────────

/// Serialisable registry account stored on-chain.
/// One `Registry` per author pubkey (derived via PDA or account address).
#[derive(Debug, Clone)]
#[cfg_attr(feature = "on-chain", derive(spel::BorshSerialize, spel::BorshDeserialize))]
pub struct Registry {
    /// Ed25519 public key of the blog author (hex-encoded, 64 chars).
    pub author_pubkey: String,
    /// Ordered list of storage CIDs for this author's published posts.
    /// Most recent post is appended last (chronological order).
    pub cids: Vec<String>,
    /// Version field for future schema migrations.
    pub version: u8,
}

impl Registry {
    pub const VERSION: u8 = 1;

    pub fn new(author_pubkey: String) -> Self {
        Self {
            author_pubkey,
            cids: Vec::new(),
            version: Self::VERSION,
        }
    }
}

// ── Instruction parameters ─────────────────────────────────────────────────────

/// Parameters for `register_post`.
#[derive(Debug, Clone)]
#[cfg_attr(feature = "on-chain", derive(spel::BorshSerialize, spel::BorshDeserialize))]
pub struct RegisterPostParams {
    /// Storage module CID returned by `uploadUrl()`.
    pub cid: String,
}

/// Parameters for `remove_post`.
#[derive(Debug, Clone)]
#[cfg_attr(feature = "on-chain", derive(spel::BorshSerialize, spel::BorshDeserialize))]
pub struct RemovePostParams {
    /// CID to remove from the author's post list.
    pub cid: String,
}

/// Parameters for `get_posts` (query — read-only, no state change).
#[derive(Debug, Clone)]
#[cfg_attr(feature = "on-chain", derive(spel::BorshSerialize, spel::BorshDeserialize))]
pub struct GetPostsParams {
    /// Ed25519 public key of the author to query (hex-encoded).
    pub author_pubkey: String,
}

// ── On-chain program (SPEL) ────────────────────────────────────────────────────

#[cfg(feature = "on-chain")]
#[lez_program]
pub mod lez_registry_program {
    use super::*;

    /// Create the registry account for the signing author.
    ///
    /// Must be called once per author before any `register_post` calls.
    /// The account is funded by the caller (author pays rent).
    #[instruction]
    pub fn initialize(ctx: Context<Initialize>) -> Result<(), ProgramError> {
        let registry = &mut ctx.accounts.registry;
        let author_pubkey = ctx.accounts.authority.key().to_string();
        **registry = Registry::new(author_pubkey);
        Ok(())
    }

    /// Append a CID to the author's post list.
    ///
    /// Only the account authority (author) can call this.
    /// Duplicate CIDs are silently ignored to keep the list idempotent.
    #[instruction]
    pub fn register_post(
        ctx: Context<AuthorAccess>,
        params: RegisterPostParams,
    ) -> Result<(), ProgramError> {
        let registry = &mut ctx.accounts.registry;

        if params.cid.is_empty() {
            return Err(ProgramError::InvalidArgument);
        }

        // Idempotent: skip if CID already present.
        if !registry.cids.contains(&params.cid) {
            registry.cids.push(params.cid);
        }

        Ok(())
    }

    /// Remove a CID from the author's post list.
    ///
    /// Used when an author deletes a post. No-op if the CID is not found.
    #[instruction]
    pub fn remove_post(
        ctx: Context<AuthorAccess>,
        params: RemovePostParams,
    ) -> Result<(), ProgramError> {
        let registry = &mut ctx.accounts.registry;
        registry.cids.retain(|c| c != &params.cid);
        Ok(())
    }

    /// Return all CIDs registered for a given author (read-only view).
    ///
    /// In SPEL, read-only instructions return data via the program return value
    /// rather than emitting an account mutation.
    ///
    /// TODO: Verify the SPEL return-value convention once the SDK stabilises.
    ///   Some frameworks use a separate "view" function type; update accordingly.
    #[instruction]
    pub fn get_posts(
        ctx: Context<ReadOnly>,
        params: GetPostsParams,
    ) -> Result<Vec<String>, ProgramError> {
        // The account passed in ctx must correspond to the requested pubkey.
        // Client-side: derive the PDA for params.author_pubkey and pass that account.
        let registry = &ctx.accounts.registry;

        if registry.author_pubkey != params.author_pubkey {
            return Err(ProgramError::InvalidAccountData);
        }

        Ok(registry.cids.clone())
    }

    // ── Account contexts ───────────────────────────────────────────────────────

    /// Accounts needed for `initialize`.
    #[derive(spel::Accounts)]
    pub struct Initialize<'info> {
        /// The registry account to create (PDA derived from authority pubkey).
        #[account(init, payer = authority, space = Registry::serialised_size())]
        pub registry: Account<'info, Registry>,
        /// The author who is creating and will own the registry.
        #[account(mut, signer)]
        pub authority: Account<'info, Pubkey>,
    }

    /// Accounts needed for mutating instructions (`register_post`, `remove_post`).
    #[derive(spel::Accounts)]
    pub struct AuthorAccess<'info> {
        /// The author's existing registry account.
        #[account(mut, has_one = authority)]
        pub registry: Account<'info, Registry>,
        /// Must match the authority stored in `registry`.
        #[account(signer)]
        pub authority: Account<'info, Pubkey>,
    }

    /// Accounts needed for read-only queries (`get_posts`).
    #[derive(spel::Accounts)]
    pub struct ReadOnly<'info> {
        /// The registry account to read from (any payer, no mutation).
        pub registry: Account<'info, Registry>,
    }
}

// TODO: Registry::serialised_size() must be implemented once BorshSerialize
// is available. Estimate: 64 (pubkey) + 4 (vec len) + N * (4 + ~60) bytes per CID
// + 1 (version) + discriminator overhead. Use a generous max CID count for the
// initial allocation; the account can be reallocated as the post list grows.
#[cfg(feature = "on-chain")]
impl Registry {
    /// Estimated upper bound for on-chain account allocation.
    /// Supports up to 1 000 CIDs of ~60 bytes each; realloc needed beyond that.
    pub fn serialised_size() -> usize {
        8           // account discriminator
        + 4 + 64    // author_pubkey String (len prefix + 64 hex chars)
        + 4 + (1_000 * (4 + 64)) // cids Vec (len prefix + up to 1000 entries)
        + 1         // version u8
    }
}

// ── Mock implementation (no SPEL runtime) ─────────────────────────────────────

/// In-memory registry for local testing (used when the `on-chain` feature is off).
#[cfg(not(feature = "on-chain"))]
pub mod mock {
    use super::{GetPostsParams, RegisterPostParams, RemovePostParams, Registry};
    use std::collections::HashMap;

    #[derive(Debug, Default)]
    pub struct MockRegistry {
        store: HashMap<String, Registry>,
    }

    impl MockRegistry {
        pub fn new() -> Self {
            Self::default()
        }

        pub fn initialize(&mut self, author_pubkey: String) {
            self.store
                .entry(author_pubkey.clone())
                .or_insert_with(|| Registry::new(author_pubkey));
        }

        pub fn register_post(
            &mut self,
            author_pubkey: &str,
            params: RegisterPostParams,
        ) -> Result<(), &'static str> {
            let reg = self
                .store
                .get_mut(author_pubkey)
                .ok_or("registry not initialised")?;
            if !reg.cids.contains(&params.cid) {
                reg.cids.push(params.cid);
            }
            Ok(())
        }

        pub fn remove_post(
            &mut self,
            author_pubkey: &str,
            params: RemovePostParams,
        ) -> Result<(), &'static str> {
            let reg = self
                .store
                .get_mut(author_pubkey)
                .ok_or("registry not initialised")?;
            reg.cids.retain(|c| c != &params.cid);
            Ok(())
        }

        pub fn get_posts(&self, params: &GetPostsParams) -> Vec<String> {
            self.store
                .get(&params.author_pubkey)
                .map(|r| r.cids.clone())
                .unwrap_or_default()
        }
    }
}

// ── Unit tests ─────────────────────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::mock::MockRegistry;
    use super::{GetPostsParams, RegisterPostParams, RemovePostParams};

    fn registry() -> MockRegistry {
        MockRegistry::new()
    }

    const AUTHOR: &str =
        "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef";
    const CID_A: &str = "bafyreiabc123";
    const CID_B: &str = "bafyreiabc456";

    #[test]
    fn initialize_creates_empty_registry() {
        let mut r = registry();
        r.initialize(AUTHOR.to_string());
        let posts = r.get_posts(&GetPostsParams {
            author_pubkey: AUTHOR.to_string(),
        });
        assert!(posts.is_empty());
    }

    #[test]
    fn register_post_appends_cid() {
        let mut r = registry();
        r.initialize(AUTHOR.to_string());
        r.register_post(AUTHOR, RegisterPostParams { cid: CID_A.to_string() })
            .unwrap();
        let posts = r.get_posts(&GetPostsParams {
            author_pubkey: AUTHOR.to_string(),
        });
        assert_eq!(posts, vec![CID_A]);
    }

    #[test]
    fn register_post_is_idempotent() {
        let mut r = registry();
        r.initialize(AUTHOR.to_string());
        r.register_post(AUTHOR, RegisterPostParams { cid: CID_A.to_string() })
            .unwrap();
        r.register_post(AUTHOR, RegisterPostParams { cid: CID_A.to_string() })
            .unwrap();
        let posts = r.get_posts(&GetPostsParams {
            author_pubkey: AUTHOR.to_string(),
        });
        assert_eq!(posts.len(), 1);
    }

    #[test]
    fn remove_post_deletes_cid() {
        let mut r = registry();
        r.initialize(AUTHOR.to_string());
        r.register_post(AUTHOR, RegisterPostParams { cid: CID_A.to_string() })
            .unwrap();
        r.register_post(AUTHOR, RegisterPostParams { cid: CID_B.to_string() })
            .unwrap();
        r.remove_post(AUTHOR, RemovePostParams { cid: CID_A.to_string() })
            .unwrap();
        let posts = r.get_posts(&GetPostsParams {
            author_pubkey: AUTHOR.to_string(),
        });
        assert_eq!(posts, vec![CID_B]);
    }

    #[test]
    fn remove_post_noop_for_unknown_cid() {
        let mut r = registry();
        r.initialize(AUTHOR.to_string());
        r.register_post(AUTHOR, RegisterPostParams { cid: CID_A.to_string() })
            .unwrap();
        // Remove a CID that was never added — should not error or panic.
        r.remove_post(
            AUTHOR,
            RemovePostParams {
                cid: "unknown-cid".to_string(),
            },
        )
        .unwrap();
        let posts = r.get_posts(&GetPostsParams {
            author_pubkey: AUTHOR.to_string(),
        });
        assert_eq!(posts, vec![CID_A]);
    }

    #[test]
    fn get_posts_returns_empty_for_unknown_author() {
        let r = registry();
        let posts = r.get_posts(&GetPostsParams {
            author_pubkey: "unknownpubkey".to_string(),
        });
        assert!(posts.is_empty());
    }

    #[test]
    fn register_fails_without_initialization() {
        let mut r = registry();
        let result =
            r.register_post(AUTHOR, RegisterPostParams { cid: CID_A.to_string() });
        assert!(result.is_err());
    }
}
