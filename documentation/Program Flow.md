# Program flow: Eyeris Increment 1

This document follows the current integrated source code. It explains how the interface coordinates the team members' work, what each stage receives and returns, and how a barcode moves through the application. The code is designed to generate and read this application's six-digit Code 128 Set B barcodes.

## Team responsibilities and handoffs

| Person | Responsibility | Main files | Handoff |
|---|---|---|---|
| Person 1 — Zaryab | Qt interface and integration | `main.cpp` | Takes user input, calls each stage, passes results onward and displays messages or results. |
| Person 2 — Nosher | Encoding | `barcodeGenerator.h`, `barcodeGenerator.cpp` | Receives six digits as `std::string`; returns a string of barcode bits. |
| Person 3 — Khurram | Barcode image creation and image saving | `BarcodePlotter.h`, `BarcodePlotter.cpp`, `BarcodeImageIO.h`, `BarcodeImageIO.cpp` | Receives the bits and original digits; produces an image for the interface to display and save. |
| Person 4 — Rayyan | Image and camera acquisition and candidate preparation | `camera-harness.h`, `camera-harness.cpp` | Receives a file path or camera frame; returns a prepared barcode image and status. |
| Person 5 — Rayan Dar | Decoding | `BarcodeDecoder.h`, `BarcodeDecoder.cpp` | Receives the prepared grayscale image; returns whether a code was found, a message and the digits. |

Person 1 calls the public functions and coordinates the handoffs. Functions within a member's portion also call their own helpers, and there are shared dependencies: the renderer and decoder reuse Person 2's validation and Code 128 functions. `ReaderContracts.h` defines the shared reader results.

## Overall flow

```text
User opens Eyeris
  ├─ Generate a barcode
  │    Enter six digits → validate → encode and verify → render image
  │    → display → user chooses whether and where to save PNG
  └─ Scan a barcode
       ├─ Upload image → open PNG/JPEG and prepare candidate ┐
       └─ Camera → capture frames and prepare candidate      ├→ decode
                                                            ┘   → display six digits
```

Errors stop or redirect the relevant branch and produce a message; a candidate is decoded only when preparation reports it is ready. Scanning is intended for one barcode at a time.

## 1. Generating a barcode

### Interface → Person 2: validate and encode

1. The user selects **Generate a barcode**, enters a code and presses the arrow or Enter. `EyerisWindow::generate()` in `main.cpp` obtains the text as a Qt `QString`. The input control limits entry to six ASCII digits; final validation also requires exactly six digits and a first digit from 1 to 9.
2. `isValidGuiInput(const QString& input)` returns a `bool`. It checks the Qt text and calls Person 2's `isValid6DigitInput(const std::string& input)` after converting the text with `toStdString()`. Invalid input is reported in the interface and no image is generated.
3. `generateBarcodeImage(const QString& input)` calls `generateVerifiedBits(const QString& input)`. This integration helper validates again, converts the code to `std::string`, and calls `generateBarcodeBits(const std::string& code6)`.
4. Person 2's `generateBarcodeBits` returns an empty string for invalid input. For valid input it builds a 121-character string of `0` and `1`, where `1` represents a black module and `0` a white module. The sequence contains a left quiet zone, Start B, six encoded digits, a checksum, a stop pattern and a right quiet zone.
5. Inside generation, `getCode128Pattern(int value)` looks up the bit pattern for a Code 128 value, and `calculateChecksum(int startValue, const std::string& code6)` calculates the weighted modulo-103 checksum. These return a `std::string` and an `int`, respectively.
6. Back in `generateVerifiedBits`, `verifyEncoding(const std::string& code6, const std::string& generatedBits)` returns `true` only if the length, quiet zones, start, six digit patterns, checksum and stop match. Failure returns an empty string to the image-generation helper.

**Handoff:** Person 2 returns the verified 121-bit `std::string` to Person 1's integration helper. Person 1 passes that string and the original six digits to Person 3.

### Person 3 → interface: render, display and save

1. `codedString2Image(const std::string& codedStr, const std::string& value, bool showText)` receives the bit string, original digits and a flag. The interface passes `true` for the flag. It returns an OpenCV `cv::Mat`, or an empty matrix on failure.
2. This function checks the bit-string length, binary characters and quiet zones. With the label enabled, it also validates the digits and checks that the bits match them. It creates a white image, calls `drawBarcode(cv::Mat& image, const std::string& codedStr, int x, int y)` to draw black bars, and calls `displayText(cv::Mat& image, const std::string& value, const cv::Rect& area)` to add the human-readable digits. These helpers modify the image by reference and return `void`.
3. Person 1's `generateBarcodeImage` passes the matrix to `mat2QImage(const cv::Mat& mat)`. This returns a copied Qt `QImage` suitable for the interface, or a null image if conversion fails. `generate()` displays the image and enables **Save to disk**. A failed generation produces an error message.
4. Saving is a separate user action, not automatic. `saveGenerated()` checks that the displayed image still matches the current input, opens a save dialog, ensures a `.png` suffix and asks before replacing an existing file. It calls `saveBarcodeImage(const QImage& image, const QString& filename)`, which returns a `bool` indicating whether Qt saved the PNG. The interface displays success or failure; cancelling the dialog leaves without saving.

## 2. Scanning an uploaded image

### Interface → Person 4: acquire and prepare

1. The user selects **Scan a barcode → Upload an Image** and chooses a PNG or JPEG. `chooseImage()` in `main.cpp` gets a `QString` file path and separately attempts to show a preview using `QImage`.
2. The interface calls `ImageSource::loadFile(const QString& path)`, which returns an `InputResult`. It checks the path and file, reads the bytes, checks for PNG or JPEG signatures, and uses OpenCV `imdecode` to obtain a color `cv::Mat`. Invalid, unsupported or unreadable files return an error status and message.
3. The path overload calls `loadFile(const cv::Mat& image)`. That overload validates the matrix, converts supported grayscale or four-channel images to three-channel color when needed, and calls the private `prepare(const cv::Mat& input)`.
4. `prepare` converts the image to grayscale and calls `locate(const cv::Mat& gray, std::vector<std::vector<cv::Point2f>>& boxes)`. The locator uses image gradients and other image-processing checks to find barcode-like regions; it returns a `bool` and fills the candidate boxes by reference.
5. With one candidate, `straighten(const cv::Mat& gray, const std::vector<cv::Point2f>& box)` prepares a straightened grayscale crop as a `cv::Mat`, preserving available image margins. `prepare` returns it in `InputResult` with status `Ready`. No candidate, multiple candidates or processing failures produce their own status and message.

**Handoff:** Person 4 returns `InputResult` to Person 1. `status` says what happened, `message` explains it, `barcode` contains the prepared grayscale candidate when ready, and `preview` is used for camera frames. Person 1 passes only a ready candidate to Person 5.

### Interface → Person 5: decode and display

1. `decodePreparedInput(const InputResult& input, const BarcodeDecoder& decoder)` handles non-ready statuses without calling the decoder. For `Ready`, it calls `BarcodeDecoder::decode(const cv::Mat& barcode)` with `input.barcode`.
2. `decode` checks that the image is a suitable 8-bit grayscale matrix and within its size limits, then uses Otsu thresholding to turn it into black and white. It samples up to 31 rows, repeating after a horizontal flip to support the opposite reading direction.
3. For each row, `decodeLine(const cv::Mat& binary, int row, std::string& code, bool& ambiguous)` measures consecutive white and black run widths and searches for a candidate. It returns whether a code was found and uses reference parameters for the code and ambiguity flag.
4. `readCandidate(const std::vector<int>& widths, std::string& code)` checks a candidate's run widths, quiet zones, Start B and stop patterns. It compares the six digit patterns, rejects ambiguous matches, checks the six-digit rule and verifies the checksum. It uses Person 2's pattern lookup, validation and checksum functions. It returns a `bool` and writes the decoded digits to `code` on success.
5. `decode` rejects conflicting readings and returns a `DecodeResult`: `found` (`bool`), `message` (`std::string`), and `code` (`std::string`, empty on failure). Person 1's `chooseImage()` calls `showDecodedCode()` for a successful result; otherwise it displays the returned message.

## 3. Scanning with the camera

1. The user selects **Scan a barcode → Use a Camera** and presses **Start Camera**. Person 1's `startCamera()` calls Person 4's `ImageSource::startCamera(int index, std::string& message)` with index `0`. The function returns a `bool` and writes a message by reference. If opening fails, the interface displays the message.
2. Once open, a Qt timer repeatedly calls Person 1's `readCameraFrame()`. It calls Person 4's `nextFrame()`, which captures a frame, converts supported formats to color if necessary, calls the same `prepare()` used for uploaded images, and returns an `InputResult`. Its `preview` contains the camera frame; its `barcode` contains the prepared candidate when ready.
3. Person 1 displays the preview. A missing candidate or multiple candidates produces guidance while scanning continues; a camera or processing error stops the camera and displays the error. When a candidate is ready, `decodePreparedInput()` passes it to Person 5's decoder exactly as in the uploaded-image branch. An unsuccessful decoding displays its message while scanning continues.
4. On success, `showDecodedCode(const DecodeResult& result)` stops the camera and shows the six digits and message. Pressing **Stop Camera** or navigating away also stops the timer and calls `ImageSource::stopCamera()` to release the camera.

## Example handoff

For input `123456`, the interface converts the `QString` to `std::string`, Person 2 returns the verified Code 128 bit string, and Person 1 passes both the bits and `123456` to Person 3. Person 3 returns an image, which Person 1 displays and can save as PNG. When that image is scanned, Person 4 returns a prepared grayscale candidate in `InputResult`; Person 1 passes it to Person 5, whose `DecodeResult` can contain `found = true` and `code = "123456"`. Person 1 then displays that code.

The description reflects the current integration candidate. Actual camera performance and platform-specific behavior depend on the camera, image quality and environment.
