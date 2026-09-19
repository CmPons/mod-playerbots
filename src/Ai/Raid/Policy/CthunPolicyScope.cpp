/* Instance-owned policy transaction and bounded host mailbox. GPL-2.0-or-later. */
#include "CthunPolicyScope.h"
#include <openssl/sha.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cmath>
#include <sstream>

namespace CthunPolicy
{
namespace
{
    bool Near(std::array<float, 3> const& a, std::array<float, 3> const& b)
    {
        return std::abs(a[0] - b[0]) < 0.1f && std::abs(a[1] - b[1]) < 0.1f && std::abs(a[2] - b[2]) < 0.1f;
    }
    bool Hex(std::string const& text, std::size_t size)
    {
        return text.size() == size && text.find_first_not_of("0123456789abcdef") == std::string::npos;
    }
    // Trusted local host mailbox only. No symlinks, nonregular files, unbounded reads or scans.
    enum class ReadState { Unavailable, Invalid, Valid };
    std::string Read(std::string const& root, char const* folder, std::string const& name, std::size_t maximum,
                     ReadState* result = nullptr)
    {
        if (result)
            *result = ReadState::Unavailable;
        int const parent = open(root.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (parent < 0)
            return {};
        int const dir = openat(parent, folder, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        close(parent);
        if (dir < 0)
            return {};
        int const file = openat(dir, name.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
        int const openError = errno;
        close(dir);
        if (file < 0)
        {
            if (result && openError == ELOOP)
                *result = ReadState::Invalid;
            return {};
        }
        if (result)
            *result = ReadState::Invalid;
        struct stat info{};
        std::string bytes;
        if (!fstat(file, &info) && S_ISREG(info.st_mode) && info.st_size > 0 &&
            static_cast<std::size_t>(info.st_size) <= maximum)
        {
            bytes.resize(maximum + 1);
            ssize_t const count = read(file, bytes.data(), bytes.size());
            if (count == info.st_size)
            {
                bytes.resize(static_cast<std::size_t>(count));
                if (result)
                    *result = ReadState::Valid;
            }
            else
            {
                bytes.clear();
                if (result && count < 0)
                    *result = ReadState::Unavailable;
            }
        }
        close(file);
        return bytes;
    }
    std::string Escape(std::string text)
    {
        for (char& c : text)
            if (c < 32 || c == '"' || c == '\\')
                c = ' ';
        return text.substr(0, 200);
    }
}
std::string Digest(std::string const& source)
{
    unsigned char bytes[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<unsigned char const*>(source.data()), source.size(), bytes);
    std::string result;
    for (unsigned char c : bytes)
    {
        result += "0123456789abcdef"[c >> 4];
        result += "0123456789abcdef"[c & 15];
    }
    return result;
}
Scope::Scope(std::string scopeId, std::string root, std::string statusRoot)
    : id(std::move(scopeId)), directory(std::move(root)), statusDirectory(std::move(statusRoot)) { }
Scope::~Scope()
{
    // Map's DataMap is the only owner. Destruction is after map update work has completed.
    // No separate OnDestroyMap close, and status failure cannot prevent Lua teardown.
    try { Status(true); } catch (...) { }
}
void Scope::Acknowledge(std::string const& guid)
{
    for (unsigned i = 0; i < adopted; ++i)
        if (_adopted[i] == guid)
            return;
    if (adopted < MAX_MEMBERS)
        _adopted[adopted++] = guid;
}
void Scope::ClearQueue()
{
    queued.clear();
    _retrySource = false;
}
void Scope::PollDefault()
{
    std::string const manifest = Read(directory, "defaults", "raid.txt", 256);
    std::istringstream input(manifest);
    std::string format, api, nonce, next, extra;
    input >> format >> api >> nonce >> next;
    bool const valid = format == "1" && api == "1" && Hex(nonce, 16) && Hex(next, 64) && !(input >> extra);
    std::string const publication = valid ? nonce + ":" + next : "";
    defaultError = valid ? "" : manifest.empty() ?
        "default missing/unreadable/byte budget" : "invalid default manifest";
    if (publication != defaultPublication)
    {
        if (diagnosticOverride || (!queued.empty() && _queuedDiagnostic))
            error = diagnosticError = "diagnostic expired: default publication changed";
        diagnosticOverride = false;
        ClearQueue();
        defaultPublication = publication;
    }
    defaultRevision = valid ? next : "";
    if (!diagnosticOverride && (queued.empty() || !_queuedDiagnostic))
    {
        desired = defaultRevision;
        desiredSource = "default";
    }
}
void Scope::SelectDefault()
{
    if (diagnosticOverride || (!queued.empty() && _queuedDiagnostic))
        return;
    desiredSource = "default";
    desired = defaultRevision;
    if (desired.empty())
        return;
    if (desired == faultedRevision)
    {
        error = "default revision quarantined after active fault";
        return;
    }
    if (active && revision == desired)
    {
        activeSource = "default";
        return; // No replacement or Lua-state reset for identical good bytes.
    }
    if (queued.empty() && _attemptedDefault != defaultPublication)
    {
        queued = desired;
        _expected = revision;
        _queuedPublication = defaultPublication;
        _queuedDiagnostic = false;
    }
}
void Scope::Poll()
{
    PollDefault();
    SelectDefault();
    std::string const request = Read(directory, "requests", id + ".txt", 256);
    if (request.empty() || request == _seen)
        return;
    _seen = request;
    std::istringstream input(request);
    std::string nonce, expected, next, publication, extra;
    input >> nonce >> expected >> next;
    input >> publication; // Legacy three-field requests are diagnostic only without an installed default.
    if (!Hex(nonce, 16) || (expected != "native" && !Hex(expected, 64)) ||
        !Hex(next, 64) || (input >> extra))
    {
        error = diagnosticError = "invalid request";
        return;
    }
    std::string const observed = defaultPublication.empty() ? "none" : defaultPublication;
    if ((publication.empty() && !defaultPublication.empty()) ||
        (!publication.empty() && publication != observed))
    {
        error = diagnosticError = "diagnostic default publication mismatch";
        return;
    }
    if (expected != revision)
    {
        error = diagnosticError = "expected revision mismatch";
        return;
    }
    if (next == faultedRevision)
    {
        error = diagnosticError = "requested revision quarantined after active fault";
        return;
    }
    ClearQueue();
    _expected = expected;
    queued = desired = next;
    desiredSource = "diagnostic";
    _queuedPublication = defaultPublication;
    _queuedDiagnostic = true;
    diagnosticError.clear();
    error.clear();
}
bool Scope::SpendPath(std::string const& guid)
{
    for (std::size_t i = 0; i < snapshot.count; ++i)
    {
        if (guid != snapshot.members[i].guid || !snapshot.members[i].eligible)
            continue;
        if (paths >= 64 || _pathUses[i] >= 8)
            return false;
        // Extra attempts cannot consume the first opportunity of a later eligible roster member.
        unsigned unserved = 0;
        for (std::size_t j = 0; j < snapshot.count; ++j)
            unserved += snapshot.members[j].eligible && !_pathUses[j] && j != i;
        if (_pathUses[i] && 64 - paths <= unserved)
            return false;
        ++_pathUses[i];
        ++paths;
        return true;
    }
    return false;
}
void Scope::RejectRoute(std::string const& guid, Candidate const& origin, Candidate const& destination)
{
    for (std::size_t i = 0; i < snapshot.count; ++i)
    {
        if (guid != snapshot.members[i].guid)
            continue;
        auto const& observed = snapshot.members[i].candidates[0];
        if (!Near({origin.x, origin.y, origin.z}, {observed.x, observed.y, observed.z}))
            return; // A moving actor needs fresh observations, not rejection learned at an old origin.
        auto& failed = _failedRoutes[i];
        if (!failed.count)
        {
            failed.guid = guid;
            failed.origin = {origin.x, origin.y, origin.z};
            failed.since = plannedAt;
        }
        std::array<float, 3> const point{destination.x, destination.y, destination.z};
        bool duplicate = false;
        for (std::size_t j = 0; j < failed.count; ++j)
            duplicate = duplicate || Near(failed.destinations[j], point);
        if (!duplicate && failed.count < MAX_CANDIDATES)
            failed.destinations[failed.count++] = point;
        plan.choices[i] = 0; // Reject this intent until Lua sees the remaining candidates next update.
        return;
    }
}
void Scope::Update(Snapshot const& current, bool safeBoundary, uint32_t now)
{
    _safeBoundary = safeBoundary;
    paths = 0;
    _pathUses = {};
    auto const previousFailures = _failedRoutes;
    _failedRoutes = {};
    Snapshot available = current;
    if (snapshot.eye == current.eye && snapshot.combat == current.combat)
        for (std::size_t i = 0; i < current.count; ++i)
        {
            auto& member = available.members[i];
            if (!member.eligible || !member.count)
                continue;
            auto const& origin = member.candidates[0];
            for (auto const& previous : previousFailures)
                if (previous.count && previous.guid == member.guid &&
                    Near(previous.origin, {origin.x, origin.y, origin.z}))
                {
                    _failedRoutes[i] = previous;
                    _failedRoutes[i].count = 0; // Retain only failures in the current finite candidate pool.
                    std::size_t retained = 1;
                    for (std::size_t j = 1; j < member.count; ++j)
                    {
                        auto const& point = member.candidates[j];
                        bool rejected = false;
                        for (std::size_t k = 0; k < previous.count; ++k)
                            if (Near(previous.destinations[k], {point.x, point.y, point.z}))
                            {
                                _failedRoutes[i].destinations[_failedRoutes[i].count++] = previous.destinations[k];
                                rejected = true;
                                break;
                            }
                        if (!rejected)
                            member.candidates[retained++] = point;
                    }
                    member.count = retained;
                    break;
                }
        }
    bool committed = false;
    excluded = 0;
    std::size_t const memberCount = api == 2 ? current.raid.count : current.count;
    for (std::size_t i = 0; i < memberCount; ++i)
        excluded += !(api == 2 ? current.raid.members[i].eligible : current.members[i].eligible);
    unsigned retained = 0;
    for (unsigned i = 0; i < adopted; ++i)
        for (std::size_t j = 0; j < memberCount; ++j)
        {
            bool const eligible = api == 2 ? current.raid.members[j].eligible : current.members[j].eligible;
            std::string const guid = api == 2 ? std::to_string(current.raid.members[j].unit.guid) : current.members[j].guid;
            if (eligible && _adopted[i] == guid)
            {
                _adopted[retained++] = _adopted[i];
                break;
            }
        }
    adopted = retained;
    if (!queued.empty() && safeBoundary && (!_retrySource || uint32_t(now - _retrySince) >= 30000))
    {
        // One extra bounded manifest read per candidate attempt, not a per-tick filesystem poll.
        // Publication after this observation is seen next poll; filesystem revocation is not a lock.
        PollDefault();
        if (!queued.empty())
        {
            bool retry = false;
            if (_queuedDiagnostic && _expected != revision)
                error = "queued expected revision mismatch";
            else if (queued == faultedRevision)
                error = "queued revision quarantined after active fault";
            else
            {
                ReadState state;
                std::string const source = Read(directory, "revisions", queued + ".lua", MAX_SOURCE, &state);
                if (state == ReadState::Unavailable)
                {
                    error = "source unavailable; retry after 30s at safe boundary";
                    retry = true;
                    _retrySince = now;
                }
                else if (state != ReadState::Valid || Digest(source) != queued)
                    error = "missing/invalid source hash or byte budget";
                else
                {
                    std::unique_ptr<Runtime> candidate = std::make_unique<Runtime>();
                    Plan checked;
                    if (!candidate->Load(source) || !candidate->Evaluate(current, checked))
                        error = "reload rejected: " + candidate->Error();
                    else
                    {
                        api = candidate->Api();
                        _runtime = std::move(candidate);
                        revision = queued;
                        faultedRevision.clear(); // Only adoption of distinct verified bytes ends quarantine.
                        faultError.clear();
                        diagnosticOverride = _queuedDiagnostic;
                        activeSource = _queuedDiagnostic ? "diagnostic" : "default";
                        ++generation;
                        adopted = 0;
                        active = true;
                        plan = checked;
                        plannedAt = now;
                        committed = true;
                        _failedRoutes = {};
                        error.clear();
                    }
                }
            }
            _retrySource = retry;
            if (_queuedDiagnostic)
                diagnosticError = committed ? "" : error;
            if (!retry)
            {
                if (!_queuedDiagnostic && !committed)
                    _attemptedDefault = _queuedPublication;
                ClearQueue();
                if (!diagnosticOverride)
                {
                    desired = defaultRevision;
                    desiredSource = "default";
                }
            }
        }
    }
    snapshot = committed ? current : available;
    if (active && !committed)
    {
        Plan checked;
        if (_runtime->Evaluate(snapshot, checked))
        {
            plan = checked;
            plannedAt = now;
            // Retry old failures only once Lua has exhausted its useful alternatives. Expiring
            // while it still proposes progress could repeatedly starve later candidates on slow bots.
            for (std::size_t i = 0; i < snapshot.count; ++i)
                if (plan.choices[i] == 0 && _failedRoutes[i].count &&
                    uint32_t(now - _failedRoutes[i].since) >= 1000)
                    _failedRoutes[i] = {};
        }
        else
        {
            error = faultError = "active policy failed: " + _runtime->Error();
            if (activeSource == "diagnostic")
                diagnosticError = error;
            active = false;
            adopted = 0;
            faultedRevision = revision;
            revision = "native";
            activeSource = "native";
            diagnosticOverride = false;
            // A pending diagnostic still describes the requested revision until its CAS is rejected.
            if (queued.empty() || !_queuedDiagnostic)
            {
                desired = defaultRevision;
                desiredSource = "default";
            }
            ++generation;
            plan = {};
            _failedRoutes = {};
            _runtime.reset();
        }
    }
}
void Scope::Status(bool destroyed) const
{
    int const dir = open(statusDirectory.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (dir < 0)
        return;
    std::ostringstream out;
    auto const time = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    out << "{\"scope\":\"" << id << "\",\"updated\":" << time
        << ",\"destroyed\":" << (destroyed ? "true" : "false")
        << ",\"active\":\"" << (active && !destroyed ? revision : "native")
        << "\",\"queued\":\"" << queued << "\",\"generation\":" << generation
        << ",\"default_publication\":\"" << defaultPublication << "\",\"default_revision\":\"" << defaultRevision
        << "\",\"default_error\":\"" << Escape(defaultError) << "\",\"desired\":\"" << desired
        << "\",\"desired_source\":\"" << desiredSource << "\",\"active_source\":\""
        << (destroyed ? "native" : activeSource) << "\",\"diagnostic_override\":"
        << (diagnosticOverride && !destroyed ? "true" : "false")
        << ",\"faulted_revision\":\"" << faultedRevision << "\",\"retry_source\":"
        << (_retrySource ? "true" : "false") << ",\"safe_boundary\":" << (_safeBoundary ? "true" : "false")
        << ",\"fault_error\":\"" << Escape(faultError) << "\""
        << ",\"diagnostic_error\":\"" << Escape(diagnosticError) << "\""
        << ",\"adopted\":" << adopted << ",\"excluded\":" << excluded
        << ",\"members\":" << (api == 2 ? snapshot.raid.count : snapshot.count)
        << ",\"api\":" << api << ",\"sampled_at\":" << snapshot.raid.sampledAt
        << ",\"observation_gaps\":" << snapshot.raid.gaps << ",\"receipts\":[";
    if (api == 2)
        for (unsigned i = 0; i < snapshot.raid.count; ++i)
        {
            auto const& member = snapshot.raid.members[i];
            if (i)
                out << ',';
            out << "{\"member\":\"" << member.unit.guid << "\",\"movement\":" << member.movementReceipt
                << ",\"action\":" << member.actionReceipt << '}';
        }
    out << "],\"error\":\"" << Escape(error) << "\"}\n";
    std::string const text = out.str();
    std::string const temporary = id + ".tmp", destination = id + ".json";
    int const file = openat(dir, temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (file >= 0)
    {
        bool const written = write(file, text.data(), text.size()) == static_cast<ssize_t>(text.size());
        close(file);
        if (written)
            renameat(dir, temporary.c_str(), dir, destination.c_str());
    }
    close(dir);
}
}
