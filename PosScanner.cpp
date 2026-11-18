#include "PosScanner.h"
#include "camera-harness.h"
#include "BarcodeDecoder.h"

#include <string>
using namespace std;

PosScanner::PosScanner(ImageSource& source, const BarcodeDecoder& decoder)
    :source_(source), decoder_(decoder), lastCode_(""), running(0){}

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
    if (input.status == ImageStatus::CameraError || input.status == ImageStatus::ProcessingError) {
        stop();
        return {false, input.message.empty() ? "Camera capture failure." : input.message, {}};
    }

    ScanResult result;
    result.preview = input.preview;

    if (input.status == ImageStatus::Ready) {
        // Run existing custom Code 128 decoder
        DecodeResult dec = decoder_.decode(input.barcode);
        result.decoded = dec;

        if (dec.found) {
            // codeChanged is true only when the detected barcode differs from lastCode_
            if (dec.code != lastCode_) {

                result.codeChanged = true;
                lastCode_ = dec.code;
            } else {
                
                result.codeChanged = false;
            }
        } else {
            result.codeChanged = false;
        }
    } else {
        // No barcode or multiple candidates: normal outcome, keep camera running
        result.decoded.found = false;
        result.decoded.code.clear();
        result.decoded.message = input.message;
        result.codeChanged = false;
    }

    return {true, "Frame processed.", result};
}