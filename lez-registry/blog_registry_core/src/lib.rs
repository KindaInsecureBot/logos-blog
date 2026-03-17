//! # blog_registry_core — Shared types for the Logos Blog LEZ registry
//!
//! This crate contains the data types, instruction parameters, and a
//! fully-functional in-memory mock used for local testing. The on-chain SPEL
//! program entry point lives in `methods/guest/src/bin/blog_registry.rs`.
//!
//! ## CID cap
//!
//! Each author registry is capped at [`Registry::MAX_CIDS`] entries (10 000).
//! When the cap is reached the **oldest** CID (index 0) is evicted before the
//! new one is appended, keeping total storage bounded on-chain.
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

// ── Constants ─────────────────────────────────────────────────────────────────

/// Maximum number of CIDs stored per author.
/// When this limit is reached the oldest entry is evicted to make room.
pub const MAX_CIDS_PER_AUTHOR: usize = 10_000;

// ── Data types (shared between on-chain and mock) ──────────────────────────────

/// Serialisable registry account stored on-chain.
/// One `Registry` per author pubkey (derived via PDA or account address).
#[derive(Debug, Clone)]
#[cfg_attr(feature = "on-chain", derive(spel::BorshSerialize, spel::BorshDeserialize))]
pub struct Registry {
    /// Ed25519 public key of the blog author (hex-encoded, 64 chars).
    pub author_pubkey: String,
    /// Ordered list of storage CIDs for this author's published posts.
    /// Oldest post is at index 0 (chronological order). Capped at MAX_CIDS.
    pub cids: Vec<String>,
    /// Version field for future schema migrations.
    pub version: u8,
}

impl Registry {
    pub const VERSION: u8 = 1;
    /// Hard cap on CIDs per author. Oldest entry is evicted when exceeded.
    pub const MAX_CIDS: usize = MAX_CIDS_PER_AUTHOR;

    pub fn new(author_pubkey: String) -> Self {
        Self {
            author_pubkey,
            cids: Vec::new(),
            version: Self::VERSION,
        }
    }

    /// Append a CID, evicting the oldest if the cap is reached.
    /// Duplicate CIDs are silently ignored (idempotent).
    pub fn push_cid(&mut self, cid: String) {
        if self.cids.contains(&cid) {
            return;
        }
        if self.cids.len() >= Self::MAX_CIDS {
            // Evict oldest (first) entry to stay within the cap.
            self.cids.remove(0);
        }
        self.cids.push(cid);
    }

    /// Remove a CID. No-op if the CID is not present.
    pub fn remove_cid(&mut self, cid: &str) {
        self.cids.retain(|c| c != cid);
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

// ── On-chain account size helper ───────────────────────────────────────────────

#[cfg(feature = "on-chain")]
impl Registry {
    /// Estimated upper bound for on-chain account allocation.
    /// Supports up to MAX_CIDS CIDs of ~60 bytes each; realloc needed if CIDs
    /// are consistently longer than 60 bytes (e.g. 64-char base58 CIDv1).
    ///
    /// TODO: Verify exact Borsh layout and adjust once BorshSerialize is stable.
    pub fn serialised_size() -> usize {
        8                                       // account discriminator
        + 4 + 64                                // author_pubkey (len prefix + 64 hex chars)
        + 4 + (MAX_CIDS_PER_AUTHOR * (4 + 64)) // cids Vec (len prefix + N entries)
        + 1                                     // version u8
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
            if params.cid.is_empty() {
                return Err("CID must not be empty");
            }
            let reg = self
                .store
                .get_mut(author_pubkey)
                .ok_or("registry not initialised")?;
            reg.push_cid(params.cid);
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
            reg.remove_cid(&params.cid);
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
    use super::{GetPostsParams, RegisterPostParams, RemovePostParams, Registry};

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

    #[test]
    fn register_post_rejects_empty_cid() {
        let mut r = registry();
        r.initialize(AUTHOR.to_string());
        let result = r.register_post(AUTHOR, RegisterPostParams { cid: String::new() });
        assert!(result.is_err());
    }

    #[test]
    fn cap_evicts_oldest_on_overflow() {
        let mut reg = Registry::new(AUTHOR.to_string());
        // Fill to MAX_CIDS.
        for i in 0..Registry::MAX_CIDS {
            reg.push_cid(format!("cid-{}", i));
        }
        assert_eq!(reg.cids.len(), Registry::MAX_CIDS);
        // The first CID should be "cid-0".
        assert_eq!(reg.cids[0], "cid-0");

        // Push one more — should evict "cid-0".
        reg.push_cid("cid-overflow".to_string());
        assert_eq!(reg.cids.len(), Registry::MAX_CIDS);
        assert_eq!(reg.cids[0], "cid-1");
        assert_eq!(reg.cids[reg.cids.len() - 1], "cid-overflow");
    }
}
