#include "ManagerPinService.h"

#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QString>

// Different platforms/packages install QtKeychain headers under qt6keychain or qtkeychain.
#if __has_include(<qt6keychain/keychain.h>)
#include <qt6keychain/keychain.h>
#else
#include <qtkeychain/keychain.h>
#endif

#include <algorithm>
#include <utility>

using namespace std;

// File-local helpers and constants (internal to ManagerPinService.cpp only).
namespace {

// Service and key name under which the PIN is stored in macOS Keychain / Windows Credential Store.
const QString kServiceName = QStringLiteral("Eyeris");
const QString kPinKey = QStringLiteral("manager-pin");

// Separates keychain accessibility (`success`) from whether a PIN has been set yet (`found`),
// so a missing first-time PIN is not treated as a credential-store failure.
struct StoredPinRead {
    bool success = false;
    bool found = false;
    string pin {};
    string error {};
};

// Accepts exactly four ASCII decimal digits, preserving leading zeros.
bool isValidPin(const string& pin)
{
    return pin.size() == 4 &&
           all_of(pin.begin(), pin.end(), [](unsigned char ch) {
               return ch >= '0' && ch <= '9';
           });
}

// Compares two validated 4-digit PIN strings without short-circuiting.
bool pinsMatch(const string& expected, const string& supplied)
{
    if (expected.size() != supplied.size()) {
        return false;
    }
    unsigned char diff = 0;
    for (size_t i = 0; i < expected.size(); ++i) {
        diff |= static_cast<unsigned char>(expected[i] ^ supplied[i]);
    }
    return diff == 0;
}

// Uses QtKeychain's error description when available, otherwise falls back to a friendly message.
string jobErrorMessage(const QKeychain::Job& job, const string& fallback)
{
    const string text = job.errorString().trimmed().toStdString();
    return text.empty() ? fallback : text;
}

} // namespace

struct ManagerPinService::State {
    bool serviceAlive = true;
    bool ownerAlive = true;
    bool hadOwner = false;
    bool busy = false;
    QPointer<QObject> owner {};
    QMetaObject::Connection ownerDestroyedConn {};

    static bool hasActiveOwner(const shared_ptr<State>& state)
    {
        return state && state->serviceAlive && state->ownerAlive &&
               (!state->hadOwner || !state->owner.isNull());
    }

    template <typename ResultT>
    static bool checkPreconditions(const shared_ptr<State>& state,
                                   const function<void(ResultT)>& done,
                                   const ResultT& ownerError,
                                   const ResultT& busyError)
    {
        if (!done) {
            return false;
        }
        if (!hasActiveOwner(state)) {
            done(ownerError);
            return false;
        }
        if (state->busy) {
            done(busyError);
            return false;
        }
        return true;
    }

    // Starts an asynchronous OS keychain lookup and invokes `done` on the Qt event loop when finished.
    static void readStoredPin(const shared_ptr<State>& state,
                              function<void(StoredPinRead)> done)
    {
        if (!hasActiveOwner(state)) {
            if (state) {
                state->busy = false;
            }
            return;
        }

        QObject* jobParent = state->hadOwner ? state->owner.data() : nullptr;

        // Parented to the service's QObject owner; QtKeychain jobs also auto-delete after finishing.
        auto* job = new QKeychain::ReadPasswordJob(kServiceName, jobParent);
        job->setKey(kPinKey);
        job->setInsecureFallback(false);

        auto settled = make_shared<bool>(false);

        // Runs asynchronously when the OS credential store responds so the UI thread never blocks.
        QObject::connect(job, &QKeychain::Job::finished, job,
            [job, state, settled, done = std::move(done)](QKeychain::Job*) {
                if (*settled) {
                    return;
                }
                *settled = true;
                if (!hasActiveOwner(state) || !done) {
                    if (state) {
                        state->busy = false;
                    }
                    return;
                }
                if (job->error() == QKeychain::NoError) {
                    // Credential exists and was read cleanly.
                    done({true, true, job->textData().toStdString(), {}});
                    return;
                }
                if (job->error() == QKeychain::EntryNotFound) {
                    // Keychain is accessible, but no manager PIN has been initialized yet.
                    done({true, false, {}, {}});
                    return;
                }
                // Any other error (e.g. denied access or locked keychain) fails closed.
                done({false, false, {},
                      jobErrorMessage(*job, "Could not read manager PIN from the OS credential store.")});
            });

        QObject::connect(job, &QObject::destroyed, job, [state, settled]() {
            if (!*settled) {
                *settled = true;
                if (state) {
                    state->busy = false;
                }
            }
        });

        // Schedules the job and returns immediately; the `finished` callback delivers the result later.
        job->start();
    }

    // Writes `pin` into the OS credential store without insecure plaintext fallback.
    static void writeStoredPin(const shared_ptr<State>& state,
                               const string& pin,
                               const string& fallbackError,
                               function<void(Status)> done)
    {
        if (!hasActiveOwner(state)) {
            if (state) {
                state->busy = false;
            }
            return;
        }

        QObject* jobParent = state->hadOwner ? state->owner.data() : nullptr;
        auto* job = new QKeychain::WritePasswordJob(kServiceName, jobParent);
        job->setKey(kPinKey);
        job->setInsecureFallback(false);
        job->setTextData(QString::fromStdString(pin));

        auto settled = make_shared<bool>(false);

        QObject::connect(job, &QKeychain::Job::finished, job,
            [job, state, settled, fallbackError, done = std::move(done)](QKeychain::Job*) {
                if (*settled) {
                    return;
                }
                *settled = true;
                if (!hasActiveOwner(state) || !done) {
                    if (state) {
                        state->busy = false;
                    }
                    return;
                }
                state->busy = false;
                if (job->error() == QKeychain::NoError) {
                    done({true, {}});
                    return;
                }
                done({false, jobErrorMessage(*job, fallbackError)});
            });

        QObject::connect(job, &QObject::destroyed, job, [state, settled]() {
            if (!*settled) {
                *settled = true;
                if (state) {
                    state->busy = false;
                }
            }
        });

        job->start();
    }
};

// Stores the QObject owner used to parent asynchronous QtKeychain jobs.
ManagerPinService::ManagerPinService(QObject* parent)
    : parent_(parent), state_(make_shared<State>())
{
    state_->hadOwner = (parent_ != nullptr);
    state_->owner = parent_;
    if (parent_ != nullptr) {
        weak_ptr<State> weakState = state_;
        state_->ownerDestroyedConn = QObject::connect(
            parent_, &QObject::destroyed, parent_,
            [weakState](QObject*) {
                if (auto s = weakState.lock()) {
                    s->ownerAlive = false;
                    s->busy = false;
                }
            });
    }
}

ManagerPinService::~ManagerPinService()
{
    if (state_) {
        state_->serviceAlive = false;
        state_->busy = false;
        if (state_->ownerDestroyedConn) {
            QObject::disconnect(state_->ownerDestroyedConn);
        }
    }
}

// Reports via `done` whether a PIN exists (`Result<bool>::value`), failing if the keychain cannot be read.
void ManagerPinService::hasPin(function<void(Result<bool>)> done)
{
    if (!State::checkPreconditions<Result<bool>>(
            state_, done,
            {false, "Manager PIN service owner is no longer available.", false},
            {false, "A manager PIN operation is already in progress.", false})) {
        return;
    }

    state_->busy = true;
    auto state = state_;
    State::readStoredPin(state, [state, done = std::move(done)](const StoredPinRead& read) {
        state->busy = false;
        if (!read.success) {
            done({false, read.error, false});
            return;
        }
        done({true, {}, read.found});
    });
}

// Validates and writes the initial 4-digit PIN only when no credential exists yet.
void ManagerPinService::initializePin(const string& pin, function<void(Status)> done)
{
    if (!State::checkPreconditions<Status>(
            state_, done,
            {false, "Manager PIN service owner is no longer available."},
            {false, "A manager PIN operation is already in progress."})) {
        return;
    }
    if (!isValidPin(pin)) {
        done({false, "Manager PIN must be exactly 4 digits."});
        return;
    }

    state_->busy = true;
    auto state = state_;
    State::readStoredPin(state, [state, pin, done = std::move(done)](const StoredPinRead& read) mutable {
        if (!read.success) {
            state->busy = false;
            done({false, read.error});
            return;
        }
        if (read.found) {
            state->busy = false;
            done({false, "A manager PIN is already configured."});
            return;
        }
        State::writeStoredPin(state, pin, "Could not save manager PIN to the OS credential store.",
                              std::move(done));
    });
}

// Validates the 4-digit format and compares the input against the stored manager PIN.
void ManagerPinService::verifyPin(const string& pin, function<void(Status)> done)
{
    if (!State::checkPreconditions<Status>(
            state_, done,
            {false, "Manager PIN service owner is no longer available."},
            {false, "A manager PIN operation is already in progress."})) {
        return;
    }
    if (!isValidPin(pin)) {
        done({false, "Manager PIN must be exactly 4 digits."});
        return;
    }

    state_->busy = true;
    auto state = state_;
    State::readStoredPin(state, [state, pin, done = std::move(done)](const StoredPinRead& read) {
        state->busy = false;
        if (!read.success) {
            done({false, read.error});
            return;
        }
        if (!read.found) {
            done({false, "Manager PIN has not been set up yet."});
            return;
        }
        if (!isValidPin(read.pin)) {
            done({false, "Stored manager PIN in the OS credential store is invalid."});
            return;
        }
        if (!pinsMatch(read.pin, pin)) {
            done({false, "Incorrect manager PIN."});
            return;
        }
        done({true, {}});
    });
}

// Verifies currentPin, validates newPin, and replaces the stored credential on success.
void ManagerPinService::changePin(const string& currentPin, const string& newPin,
                                  function<void(Status)> done)
{
    if (!State::checkPreconditions<Status>(
            state_, done,
            {false, "Manager PIN service owner is no longer available."},
            {false, "A manager PIN operation is already in progress."})) {
        return;
    }
    if (!isValidPin(currentPin)) {
        done({false, "Current manager PIN must be exactly 4 digits."});
        return;
    }
    if (!isValidPin(newPin)) {
        done({false, "New manager PIN must be exactly 4 digits."});
        return;
    }

    state_->busy = true;
    auto state = state_;
    State::readStoredPin(state, [state, currentPin, newPin, done = std::move(done)](const StoredPinRead& read) mutable {
        if (!read.success) {
            state->busy = false;
            done({false, read.error});
            return;
        }
        if (!read.found) {
            state->busy = false;
            done({false, "Manager PIN has not been set up yet."});
            return;
        }
        if (!isValidPin(read.pin)) {
            state->busy = false;
            done({false, "Stored manager PIN in the OS credential store is invalid."});
            return;
        }
        if (!pinsMatch(read.pin, currentPin)) {
            state->busy = false;
            done({false, "Current manager PIN is incorrect."});
            return;
        }
        State::writeStoredPin(state, newPin, "Could not update manager PIN in the OS credential store.",
                              std::move(done));
    });
}

// Verifies the PIN and binds the exact PaidEditDraft into a ManagerApproval.
void ManagerPinService::approvePaidEdit(const string& pin, const PaidEditDraft& draft,
                                        function<void(Result<ManagerApproval>)> done)
{
    if (!State::checkPreconditions<Result<ManagerApproval>>(
            state_, done,
            {false, "Manager PIN service owner is no longer available.", {}},
            {false, "A manager PIN operation is already in progress.", {}})) {
        return;
    }
    if (!isValidPin(pin)) {
        done({false, "Manager PIN must be exactly 4 digits.", {}});
        return;
    }
    if (draft.orderId.empty()) {
        done({false, "A valid paid edit draft is required for manager approval.", {}});
        return;
    }

    // Freeze an immutable copy of the draft before the async keychain lookup begins.
    const PaidEditDraft boundDraft = draft;
    state_->busy = true;
    auto state = state_;
    State::readStoredPin(state, [state, pin, boundDraft, done = std::move(done)](const StoredPinRead& read) {
        state->busy = false;
        if (!read.success) {
            done({false, read.error, {}});
            return;
        }
        if (!read.found) {
            done({false, "Manager PIN has not been set up yet.", {}});
            return;
        }
        if (!isValidPin(read.pin)) {
            done({false, "Stored manager PIN in the OS credential store is invalid.", {}});
            return;
        }
        if (!pinsMatch(read.pin, pin)) {
            done({false, "Incorrect manager PIN.", {}});
            return;
        }

        ManagerApproval approval;
        approval.draft_ = boundDraft;
        done({true, {}, std::move(approval)});
    });
}
