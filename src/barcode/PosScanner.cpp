#include "PosScanner.h"
#include "camera-harness.h"
#include "BarcodeDecoder.h"

#include <string>
using namespace std;

PosScanner::PosScanner(ImageSource& source, const BarcodeDecoder& decoder)
    :source_(source), decoder_(decoder), lastCode_(""), running_(false){}

PosScanner::~PosScanner(){

    stop();
}

void PosScanner::stop() {

    if (running_) {

        source_.stopCamera();
        running_ = false;
    }
}

Status PosScanner::start(int cameraIndex) {
    if (running_) {
        stop();
    }

    string msg;
    if (!source_.startCamera(cameraIndex, msg)) {
        running_ = false;
        return {false, msg.empty() ? "Could not open camera." : msg};
    }

    running_ = true;
    lastCode_.clear(); // Reset lastCode_ for a new session
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
    result.preview = input.preview;

    if (input.status == ImageStatus::Ready) {
        // Run existing custom Code 128 decoder
        try {
            result.decoded = decoder_.decode(input.barcode);
        } catch (const cv::Exception&) {
            stop();
            return {false, "Could not process the barcode candidate.", {}};
        }

        if (result.decoded.found) {
            result.codeChanged = result.decoded.code != lastCode_;
            lastCode_ = result.decoded.code;
        } else {
            result.decoded.code.clear();
        }
    } else {
        // No barcode or multiple candidates: normal outcome, keep camera running
        result.decoded.found = false;
        result.decoded.code.clear();
        result.decoded.message = input.message;
        result.codeChanged = false;
    }

    return {true, result.decoded.found ? string{} : result.decoded.message, result};
}
