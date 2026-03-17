/// Logos Blog Post Registry — SPEL on-chain program
///
/// Maintains an `author_pubkey → Vec<CID>` mapping so that any node can
/// discover an author's published posts without relying on message history.
///
/// # Instructions
/// - `register_post(author_pubkey, cid)` — append a new CID for an author
/// - `remove_post(author_pubkey, cid)` — remove a CID (e.g. when post is deleted)
///
/// # Queries
/// - `get_posts(author_pubkey)` → `Vec<String>` of CIDs in insertion order
/// - `get_post_count(author_pubkey)` → `u64`
/// - `get_authors()` → `Vec<String>` of all registered author pubkeys
use lez_framework::prelude::*;

/// Maximum CIDs stored per author.  Oldest entries are evicted when the cap
/// is exceeded so state growth is bounded.
const MAX_CIDS_PER_AUTHOR: usize = 10_000;

#[lez_program]
pub mod blog_registry {
    use super::*;

    // ── On-chain state ────────────────────────────────────────────────────

    /// Root state of the registry program.
    #[state]
    pub struct BlogRegistry {
        /// Maps Ed25519 public key hex → ordered list of Storage CIDs.
        pub posts: BTreeMap<String, Vec<String>>,
    }

    impl Default for BlogRegistry {
        fn default() -> Self {
            Self {
                posts: BTreeMap::new(),
            }
        }
    }

    // ── Instructions ─────────────────────────────────────────────────────

    /// Record a new post CID for `author_pubkey`.
    ///
    /// The caller must be the key-holder for `author_pubkey` (enforced by the
    /// LEZ runtime via the transaction signature check).
    #[instruction]
    pub fn register_post(
        ctx: Context<BlogRegistry>,
        author_pubkey: String,
        cid: String,
    ) -> Result<()> {
        require!(!author_pubkey.is_empty(), ErrorCode::InvalidPubkey);
        require!(!cid.is_empty(), ErrorCode::InvalidCid);

        let registry = &mut ctx.state;
        let entry = registry.posts.entry(author_pubkey).or_default();

        // Idempotent: skip if CID is already registered
        if entry.contains(&cid) {
            return Ok(());
        }

        // Evict the oldest entry when the per-author cap is reached
        if entry.len() >= MAX_CIDS_PER_AUTHOR {
            entry.remove(0);
        }

        entry.push(cid);
        Ok(())
    }

    /// Remove a post CID for `author_pubkey` (e.g. author deleted the post).
    ///
    /// No-op if the CID is not found.
    #[instruction]
    pub fn remove_post(
        ctx: Context<BlogRegistry>,
        author_pubkey: String,
        cid: String,
    ) -> Result<()> {
        require!(!author_pubkey.is_empty(), ErrorCode::InvalidPubkey);
        require!(!cid.is_empty(), ErrorCode::InvalidCid);

        let registry = &mut ctx.state;
        if let Some(entry) = registry.posts.get_mut(&author_pubkey) {
            entry.retain(|c| c != &cid);
            if entry.is_empty() {
                registry.posts.remove(&author_pubkey);
            }
        }
        Ok(())
    }

    // ── Queries ───────────────────────────────────────────────────────────

    /// Return all CIDs registered for `author_pubkey`, in insertion order.
    ///
    /// Returns an empty `Vec` if the author has no registered posts.
    #[query]
    pub fn get_posts(
        ctx: Context<BlogRegistry>,
        author_pubkey: String,
    ) -> Vec<String> {
        ctx.state
            .posts
            .get(&author_pubkey)
            .cloned()
            .unwrap_or_default()
    }

    /// Return the number of registered CIDs for `author_pubkey`.
    #[query]
    pub fn get_post_count(
        ctx: Context<BlogRegistry>,
        author_pubkey: String,
    ) -> u64 {
        ctx.state
            .posts
            .get(&author_pubkey)
            .map(|v| v.len() as u64)
            .unwrap_or(0)
    }

    /// Return all author pubkeys that have at least one registered post.
    #[query]
    pub fn get_authors(ctx: Context<BlogRegistry>) -> Vec<String> {
        ctx.state.posts.keys().cloned().collect()
    }

    // ── Error codes ───────────────────────────────────────────────────────

    #[error_code]
    pub enum ErrorCode {
        #[msg("author_pubkey must not be empty")]
        InvalidPubkey,
        #[msg("cid must not be empty")]
        InvalidCid,
    }
}
