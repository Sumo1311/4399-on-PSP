// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace bvn {
class StageRenderer;

// The renderer must remain alive until the process exits. Install from the main
// thread before starting audio or other workers; only that thread may reclaim.
bool installGameAllocationRecovery(StageRenderer& renderer);
}
