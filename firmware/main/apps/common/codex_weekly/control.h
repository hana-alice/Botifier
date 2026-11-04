#pragma once
#include <algorithm>
#include <cstdint>

namespace codex_weekly::detail {

enum class Command { None, Login, Logout };
struct Request { Command command; std::uint32_t generation; bool refresh; };

// Called only while the service state mutex is held. A login/logout invalidates
// every previous request, including responses already received by the worker.
class CommandGate {
public:
    void login() { ++generation_; command_ = Command::Login; refresh_ = false; }
    void logout() { ++generation_; command_ = Command::Logout; refresh_ = false; }
    void refresh() { refresh_ = true; }
    bool current(std::uint32_t generation) const { return generation == generation_; }
    std::uint32_t generation() const { return generation_; }
    Request take()
    {
        const Request result{command_, generation_, refresh_};
        command_ = Command::None;
        refresh_ = false;
        return result;
    }
private:
    std::uint32_t generation_ = 1;
    Command command_ = Command::None;
    bool refresh_ = false;
};

enum class UsageAction { Accept, RefreshOnce, RetryLater, Fail };
inline UsageAction usage_action(int status, bool already_refreshed)
{
    if (status >= 200 && status < 300) return UsageAction::Accept;
    if (status == 401 && !already_refreshed) return UsageAction::RefreshOnce;
    if (status == 429 || status >= 500) return UsageAction::RetryLater;
    return UsageAction::Fail;
}

enum class RotationAction { IgnoreStale, StorageFailure, Accept };
// Persist before exposing a rotated credential. Recheck the command generation
// after storage, because login/logout can arrive while NVS commits.
template<class Current, class Store, class Accept>
RotationAction rotate(Current current, Store store, Accept accept)
{
    if (!current()) return RotationAction::IgnoreStale;
    const bool stored = store();
    if (!current()) return RotationAction::IgnoreStale;
    if (!stored) return RotationAction::StorageFailure;
    accept();
    return RotationAction::Accept;
}

class Backoff {
public:
    std::uint32_t fail(bool rate_limited)
    {
        const auto base = rate_limited ? 60000u : 15000u;
        const auto shift = failures_;
        failures_ = std::min(failures_ + 1, 5u);
        return std::min(300000u, base << shift);
    }
    void reset() { failures_ = 0; }
private:
    unsigned failures_ = 0;
};

} // namespace codex_weekly::detail
