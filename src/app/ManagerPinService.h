#pragma once

#include "PosTypes.h"
#include <functional>
#include <memory>

using namespace std;

class QObject;

// Manages the 4-digit Manager PIN in the OS credential store and issues bound paid-edit approvals.
class ManagerPinService {           // this is a separate class for security reasons, functions are not allowed to be run by everyone
private:
    struct State;

    QObject* parent_ = nullptr;
    shared_ptr<State> state_;

public:
    // Stores the QObject owner used to parent asynchronous QtKeychain jobs.
    explicit ManagerPinService(QObject* parent = nullptr);
    ~ManagerPinService();

    ManagerPinService(const ManagerPinService&) = delete;
    ManagerPinService& operator=(const ManagerPinService&) = delete;

    // Checks whether a manager PIN is stored, distinguishing missing credentials from store errors.
    void hasPin(function<void(Result<bool>)> done);

    // Validates and writes the initial 4-digit PIN only when no credential exists yet.
    void initializePin(const string& pin, function<void(Status)> done);

    // Validates the 4-digit format and compares the input against the stored manager PIN.
    void verifyPin(const string& pin, function<void(Status)> done);

    // Verifies currentPin, validates newPin, and replaces the stored credential on success.
    void changePin(const string& currentPin, const string& newPin, function<void(Status)> done);

    // Verifies the PIN and binds the exact PaidEditDraft into a ManagerApproval.
    void approvePaidEdit(const string& pin, const PaidEditDraft& draft,
                         function<void(Result<ManagerApproval>)> done);

    // done functions are to prevent the program from waiting for permsission to access keychain and hanging
};
