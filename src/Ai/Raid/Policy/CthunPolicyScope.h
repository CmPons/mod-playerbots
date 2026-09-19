/* Instance-owned policy transaction and bounded host mailbox. GPL-2.0-or-later. */
#ifndef PLAYERBOTS_CTHUN_POLICY_SCOPE_H
#define PLAYERBOTS_CTHUN_POLICY_SCOPE_H
#include "CthunPolicyRuntime.h"
#include "DataMap.h"
#include <memory>

namespace CthunPolicy
{
class Scope : public DataMap::Base
{
public:
    Scope(std::string id, std::string directory, std::string statusDirectory);
    ~Scope() override;
    void Update(Snapshot const& snapshot, bool safeBoundary, uint32_t now);
    void Poll();
    void Status(bool destroyed = false) const;
    bool Fresh(uint32_t now) const { return active && uint32_t(now - plannedAt) < 1000; }
    bool SpendPath(std::string const& guid);
    void RejectRoute(std::string const& guid, Candidate const& origin, Candidate const& destination);
    void Acknowledge(std::string const& guid);

    std::string id, directory, statusDirectory;
    std::string revision = "native", queued, error, diagnosticError;
    std::string defaultPublication, defaultRevision, defaultError, desired, desiredSource = "default";
    std::string activeSource = "native", faultedRevision, faultError;
    bool diagnosticOverride = false;
    uint64_t generation = 1;
    uint32_t plannedAt = 0, elapsed = 0, pollElapsed = 1000;
    unsigned paths = 0, adopted = 0, excluded = 0, api = 1;
    bool active = false;
    Snapshot snapshot;
    Plan plan;
private:
    std::unique_ptr<Runtime> _runtime;
    void PollDefault();
    void SelectDefault();
    void ClearQueue();
    std::string _seen, _expected, _queuedPublication, _attemptedDefault;
    bool _queuedDiagnostic = false, _retrySource = false, _safeBoundary = false;
    uint32_t _retrySince = 0;
    std::array<std::string, MAX_MEMBERS> _adopted;
    struct FailedRoutes
    {
        std::string guid;
        std::array<float, 3> origin{};
        std::array<std::array<float, 3>, MAX_CANDIDATES> destinations{};
        std::size_t count = 0;
        uint32_t since = 0;
    };
    std::array<FailedRoutes, MAX_MEMBERS> _failedRoutes;
    std::array<unsigned, MAX_MEMBERS> _pathUses{};
};
std::string Digest(std::string const& source);
}
#endif
