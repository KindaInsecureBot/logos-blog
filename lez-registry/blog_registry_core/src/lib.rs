//! # blog_registry_core — Shared types for the Logos Blog LEZ registry
//!
//! This crate contains the on-chain `Registry` data type and an in-memory mock
//! used for local testing. The SPEL on-chain program entry point lives in
//! `methods/guest/src/bin/blog_registry.rs`.
//!
//! ## CID cap
//!
//! Each author registry is capped at [`Registry::MAX_CIDS`] entries (10 000).
//! When the cap is reached the **oldest** CID (index 0) is evicted before the
//! new one is appended, keeping total storage bounded on-chain.

use borsh::{BorshDeserialize, BorshSerialize};
use serde::{Deserialize, Serialize};

// ── Constants ─────────────────────────────────────────────────────────────────

/// Maximum number of CIDs stored per author.
/// When this limit is reached the oldest entry is evicted to make room.
pub const MAX_CIDS_PER_AUTHOR: usize = 10_000;

// ── Data types ────────────────────────────────────────────────────────────────

/// Serialisable registry account stored on-chain.
/// One `Registry` per author, stored at the PDA derived from the author's account ID.
#[derive(Debug, Clone, BorshSerialize, BorshDeserialize, Serialize, Deserialize)]
pub struct Registry {
    /// Hex-encoded author account ID for informational purposes.
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
            self.cids.remove(0);
        }
        self.cids.push(cid);
    }

    /// Remove a CID. No-op if the CID is not present.
    pub fn remove_cid(&mut self, cid: &str) {
        self.cids.retain(|c| c != cid);
    }
}

// ── Mock implementation (no SPEL runtime) ─────────────────────────────────────

/// In-memory registry for local testing.
pub mod mock {
    use super::Registry;
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
            cid: String,
        ) -> Result<(), &'static str> {
            if cid.is_empty() {
                return Err("CID must not be empty");
            }
            let reg = self
                .store
                .get_mut(author_pubkey)
                .ok_or("registry not initialised")?;
            reg.push_cid(cid);
            Ok(())
        }

        pub fn remove_post(
            &mut self,
            author_pubkey: &str,
            cid: &str,
        ) -> Result<(), &'static str> {
            let reg = self
                .store
                .get_mut(author_pubkey)
                .ok_or("registry not initialised")?;
            reg.remove_cid(cid);
            Ok(())
        }

        pub fn get_posts(&self, author_pubkey: &str) -> Vec<String> {
            self.store
                .get(author_pubkey)
                .map(|r| r.cids.clone())
                .unwrap_or_default()
        }
    }
}

// ── Unit tests ─────────────────────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::mock::MockRegistry;
    use super::Registry;

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
        let posts = r.get_posts(AUTHOR);
        assert!(posts.is_empty());
    }

    #[test]
    fn register_post_appends_cid() {
        let mut r = registry();
        r.initialize(AUTHOR.to_string());
        r.register_post(AUTHOR, CID_A.to_string()).unwrap();
        let posts = r.get_posts(AUTHOR);
        assert_eq!(posts, vec![CID_A]);
    }

    #[test]
    fn register_post_is_idempotent() {
        let mut r = registry();
        r.initialize(AUTHOR.to_string());
        r.register_post(AUTHOR, CID_A.to_string()).unwrap();
        r.register_post(AUTHOR, CID_A.to_string()).unwrap();
        let posts = r.get_posts(AUTHOR);
        assert_eq!(posts.len(), 1);
    }

    #[test]
    fn remove_post_deletes_cid() {
        let mut r = registry();
        r.initialize(AUTHOR.to_string());
        r.register_post(AUTHOR, CID_A.to_string()).unwrap();
        r.register_post(AUTHOR, CID_B.to_string()).unwrap();
        r.remove_post(AUTHOR, CID_A).unwrap();
        let posts = r.get_posts(AUTHOR);
        assert_eq!(posts, vec![CID_B]);
    }

    #[test]
    fn remove_post_noop_for_unknown_cid() {
        let mut r = registry();
        r.initialize(AUTHOR.to_string());
        r.register_post(AUTHOR, CID_A.to_string()).unwrap();
        r.remove_post(AUTHOR, "unknown-cid").unwrap();
        let posts = r.get_posts(AUTHOR);
        assert_eq!(posts, vec![CID_A]);
    }

    #[test]
    fn get_posts_returns_empty_for_unknown_author() {
        let r = registry();
        let posts = r.get_posts("unknownpubkey");
        assert!(posts.is_empty());
    }

    #[test]
    fn register_fails_without_initialization() {
        let mut r = registry();
        let result = r.register_post(AUTHOR, CID_A.to_string());
        assert!(result.is_err());
    }

    #[test]
    fn register_post_rejects_empty_cid() {
        let mut r = registry();
        r.initialize(AUTHOR.to_string());
        let result = r.register_post(AUTHOR, String::new());
        assert!(result.is_err());
    }

    #[test]
    fn cap_evicts_oldest_on_overflow() {
        let mut reg = Registry::new(AUTHOR.to_string());
        for i in 0..Registry::MAX_CIDS {
            reg.push_cid(format!("cid-{}", i));
        }
        assert_eq!(reg.cids.len(), Registry::MAX_CIDS);
        assert_eq!(reg.cids[0], "cid-0");

        reg.push_cid("cid-overflow".to_string());
        assert_eq!(reg.cids.len(), Registry::MAX_CIDS);
        assert_eq!(reg.cids[0], "cid-1");
        assert_eq!(reg.cids[reg.cids.len() - 1], "cid-overflow");
    }
}
