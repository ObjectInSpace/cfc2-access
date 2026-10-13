#ifndef CFC2_GAME_SESSION_H
#define CFC2_GAME_SESSION_H
#include <stdbool.h>
#include <stdint.h>

/* Serialized state machine for the first shared selection path. No native
 * pointers, hooks, memory reads, output calls or inferred lifecycle authority.
 * All operations require one externally serialized owner. Native callbacks
 * must NOT call these concurrently. This is not a native lifecycle authority or collection-wide router.
 * Zero-initialize once; never reinitialize while tokens can still exist. */
typedef struct {
    uint64_t generation, revision, sequence;
    unsigned adapter, rom;
} game_session_token;

typedef struct {
    game_session_token issued;
    uint64_t published, consumed;
    bool owned, eligible, exhausted;
} game_session;

/* Call only with an already validated adapter/build/ROM identity. Every owner
 * acquisition creates a new generation, even for identical IDs/addresses.
 * Leaves capture disabled until an evidenced completion boundary resumes it. */
bool game_session_begin(game_session *s, unsigned adapter, unsigned rom);
/* A queued acquisition must name the generation/revision at which it was
 * authorized. Refuse it without changing state if another invalidation or
 * acquisition intervened. As with begin, identity/readiness evidence and
 * serialization belong to the caller; success still leaves output disabled. */
bool game_session_begin_if_current(game_session *s, uint64_t generation,
                                   uint64_t revision, unsigned adapter, unsigned rom);
/* Both invalidate all pending observations. Suspend retains owner identity;
 * departure also relinquishes ownership. Neither implies speech re-arming. */
void game_session_suspend(game_session *s);
void game_session_depart(game_session *s);
/* Expected labels must be obtained at the start of the enclosing operation.
 * A stale completion cannot reopen capture after a newer invalidation. */
bool game_session_resume(game_session *s, uint64_t generation, uint64_t revision);
bool game_session_capture(game_session *s, game_session_token *token);
/* Complete only after the copied snapshot is ready. Out-of-order completion
 * cannot replace a newer publication. This is latest-selection semantics,
 * not a FIFO suitable for story text. */
bool game_session_publish(game_session *s, const game_session_token *token);
bool game_session_can_output(const game_session *s, const game_session_token *token);
/* A rejected backend leaves the observation retryable. An accepted backend
 * advances only if the token still belongs to the current publication. */
bool game_session_accept(game_session *s, const game_session_token *token,
                         bool backend_accepted);
#endif
