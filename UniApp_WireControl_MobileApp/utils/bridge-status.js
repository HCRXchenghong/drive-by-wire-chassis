export function getLatestStatusTimestamp(bridgeState) {
  const state = bridgeState || {};
  const chassisStatusAt = Number(state.chassisStatusAt || 0);
  const statusAt = Number(state.statusAt || 0);

  return Math.max(chassisStatusAt, statusAt);
}

export function getLatestStatusSnapshot(bridgeState) {
  const state = bridgeState || {};
  const chassisStatusAt = Number(state.chassisStatusAt || 0);
  const statusAt = Number(state.statusAt || 0);

  if (statusAt > chassisStatusAt) {
    return state.statusData || state.chassisStatus || null;
  }

  return state.chassisStatus || state.statusData || null;
}
