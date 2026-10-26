#pragma once

// A structurally valid Steam JWT with the given subject and audiences; the signature is not checked locally.
[[nodiscard]] QString SyntheticJwt(quint64 steamId, const QStringList& audiences, qint64 expiresAtSeconds);
