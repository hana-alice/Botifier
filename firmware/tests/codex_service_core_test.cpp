#include <apps/common/codex_weekly/control.h>
#include <iostream>
#include <stdexcept>

using namespace codex_weekly::detail;
void check(bool value) { if (!value) throw std::runtime_error("codex service control regression"); }
int main()
{
    try {
        CommandGate gate;
        const auto boot = gate.take();
        gate.login();
        const auto login = gate.take();
        check(login.command == Command::Login && !gate.current(boot.generation));
        gate.logout();
        check(!gate.current(login.generation)); // In-flight OAuth/refresh must not write back.
        const auto logout = gate.take();
        check(logout.command == Command::Logout && gate.current(logout.generation));
        gate.refresh(); gate.refresh();
        check(gate.take().refresh && !gate.take().refresh); // No unbounded click queue.
        gate.login(); gate.logout(); gate.login();
        check(gate.take().command == Command::Login && !gate.current(logout.generation));
        check(usage_action(401, false) == UsageAction::RefreshOnce);
        check(usage_action(401, true) == UsageAction::Fail); // Never loop on unauthorized.
        check(usage_action(429, false) == UsageAction::RetryLater);
        check(usage_action(200, true) == UsageAction::Accept);
        bool stored = false, accepted = false;
        auto generation = gate.generation();
        auto is_current = [&] { return gate.current(generation); };
        check(rotate(is_current, [&] { stored = true; return true; }, [&] { check(stored); accepted = true; })
              == RotationAction::Accept && accepted);
        stored = accepted = false;
        check(rotate(is_current, [&] { stored = true; return false; }, [&] { accepted = true; })
              == RotationAction::StorageFailure && stored && !accepted);
        stored = false;
        gate.logout();
        check(rotate(is_current, [&] { stored = true; return true; }, [&] { accepted = true; })
              == RotationAction::IgnoreStale && !stored && !accepted);
        generation = gate.generation();
        check(rotate(is_current, [&] { stored = true; gate.login(); return true; }, [&] { accepted = true; })
              == RotationAction::IgnoreStale && stored && !accepted);
        Backoff retry;
        check(retry.fail(true) == 60000);
        for (int i = 0; i < 30; ++i) check(retry.fail(true) <= 300000);
        retry.reset(); check(retry.fail(false) == 15000);
        std::cout << "codex_service_core_test: command, unauthorized, retry and rotation groups passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
