#include "PosScanner.h"
#include "camera-harness.h"
#include "BarcodeDecoder.h"

#include <string>
#include <utility>
using namespace std;

PosScanner::PosScanner(ImageSource& source, const BarcodeDecoder& decoder)
    : source_(source), decoder_(decoder) {}

PosScanner::~PosScanner() {
    stop();
}

void PosScanner::resetAbsence() {
    absentSince_ = {};
    absentPolls_ = 0;
}

void PosScanner::resetSession() {
    lastCode_.clear();
    resetAbsence();
    rearmed_ = false;
}

void PosScanner::evaluatePresentation(ImageStatus status,
                                      chrono::steady_clock::time_point now,
                                      ScanResult& result) {
    if (status == ImageStatus::Ready && result.decoded.found && !result.decoded.code.empty()) {
        result.codeChanged = (result.decoded.code != lastCode_) || rearmed_;
        lastCode_ = result.decoded.code;
        resetAbsence();
        rearmed_ = false;
        return;
    }

    result.decoded.found = false;
    result.decoded.code.clear();
    result.codeChanged = false;

    if (status == ImageStatus::NoCandidate) {
        if (!lastCode_.empty() && !rearmed_) {
            if (absentPolls_ == 0) {
                absentSince_ = now;
            }
            if (absentPolls_ < kRearmAbsentPolls) {
                ++absentPolls_;
            }
            if (absentPolls_ >= kRearmAbsentPolls &&
                now >= absentSince_ &&
                (now - absentSince_) >= kRearmAbsentDuration) {
                rearmed_ = true;
            }
        }
        return;
    }

    // Located candidates (failed decode or multiple candidates) show recognition ambiguity, not absence.
    resetAbsence();
}

void PosScanner::stop() {
    if (running_) {
        source_.stopCamera();
        running_ = false;
    }
    resetSession();
}

Status PosScanner::start(int cameraIndex) {
    stop();

    string msg;
    if (!source_.startCamera(cameraIndex, msg)) {
        return {false, msg.empty() ? "Could not open camera." : msg};
    }

    running_ = true;
    resetSession();
    return {true, "Camera scanner started."};
}

Result<ScanResult> PosScanner::poll() {
    if (!running_) {
        return {false, "Scanner is not running.", {}};
    }

    // Grab next frame from existing camera harness
    InputResult input = source_.nextFrame();

    // hardware / frame capture failure
    if (input.status != ImageStatus::Ready && input.status != ImageStatus::NoCandidate &&
        input.status != ImageStatus::MultipleCandidates) {
        stop();
        return {false, input.message.empty() ? "Camera capture failure." : input.message, {}};
    }

    ScanResult result;
    result.preview = std::move(input.preview);

    if (input.status == ImageStatus::Ready) {
        // Run existing custom Code 128 decoder
        try {
            result.decoded = decoder_.decode(input.barcode);
        } catch (const cv::Exception&) {
            stop();
            return {false, "Could not process the barcode candidate.", {}};
        }
        if (result.decoded.found && result.decoded.code.empty()) {
            result.decoded.found = false;
        }
    } else {
        // No barcode or multiple candidates: normal outcome, keep camera running
        result.decoded.found = false;
        result.decoded.code.clear();
        result.decoded.message = input.message.empty()
            ? (input.status == ImageStatus::MultipleCandidates
                   ? "Please scan only one barcode at a time."
                   : "No barcode found.")
            : input.message;
    }

    evaluatePresentation(input.status, chrono::steady_clock::now(), result);

    return {true, result.decoded.found ? string{} : result.decoded.message, result};
}
