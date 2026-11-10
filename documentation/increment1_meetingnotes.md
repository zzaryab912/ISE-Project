# Increment 1: Bar Code Generator and Reader — Meeting Notes

| | |
|---|---|
| **Project** | Super store inventory and sales application (C++) |
| **Increment** | 1: Bar Code Generator and Reader |
| **Graded demo** | 12/11/2026 |
| **Last updated** | 09/11/2026 (after Meeting 2) |

> **How to maintain this file:** After each meeting, (1) add a new section under [Meetings](#meetings), including that meeting's decisions, (2) update statuses and add new rows in the [Action Items](#action-items) table, (3) update the [Work Division](#work-division) table if responsibilities change, and (4) change "Last updated" above.

---

## Requirements

| # | Requirement |
|---|---|
| R1 | Generate a barcode from a **6-digit number**. The input must **start with a non-zero digit**. |
| R2 | The generated barcode must be **displayed on screen** and **saved to disk as an image**. |
| R3 | **Read a barcode generated in R1** and show the 6-digit number on screen. |
| R4 | The barcode can be read from an **uploaded image** or **scanned through the laptop camera**. |
| R5 | **Existing non-standard libraries cannot be used to generate barcodes.** Libraries for image processing and file handling are allowed. |
| R6 | Submission is through **GitHub only**. |
| R7 | A **graded demo** is held in class on **12/11/2026**. |

---

## Work Division

Responsibilities as assigned after Meeting 1.

| Member | Name | Area | Responsibilities |
|---|---|---|---|
| 1 | Zaryab | Desktop interface and integration | Own the screens, navigation and user feedback; coordinate how the pieces come together and prepare the demo. |
| 2 | Nosher | Barcode generation logic | Validate six-digit input and create a Code 128 pattern using team-written code. Verify the encoding. |
| 3 | Khurram | Image output and file handling | Turn the pattern into a scannable image; make it available for preview and save it to disk. Also load the selected image file for reading and handle file errors. |
| 4 | Rayyan | Camera | Capture frames, handle camera errors and stop scanning. |
| 5 | Rayan | Barcode decoding and build setup | Connect ZXing-C++ to both image sources; return the value, format or failure. Own dependency and build setup. |

---

## Action Items

Running list. Keep completed items for the record; set their status to Done.

| # | Action | Owner | Due | Status | Raised in |
|---|---|---|---|---|---|
| A1 | Divide work among team members | Team | After Meeting 1 | Done | 1 |
| A2 | Prepare individual **low-level designs** | All members | Before 09/11/2026 | Done | 1 |
| A3 | Decide between **Qt and OpenCV** for image generation | Team | Meeting 2 | Done (Qt) | 1 |
| A4 | Create the **UML diagram**; review for overlap and interdependencies | Team | Meeting 2 | Open | 1 |
| A5 | Begin the **coding phase** | Team | From 09/11/2026 | Open | 1 |
| A6 | Have a working Increment 1 ready for the **graded demo** | Team | 12/11/2026 | Open | 1 |
| A7 | **Finalize code** for each member's part | All members | 10/11/2026 (night) | Open | 2 |
| A8 | **Integrate** all parts and **test** the application | Team | 11/11/2026 | Open | 2 |
| A9 | Write **test cases** to verify outputs (suggested, optional) | Team | During integration and testing | Open | 2 |

---

## Meetings

### Meeting 1: Kickoff and Design Discussion (08/11/2026)

**Agenda**

1. Choice of barcode symbology
2. Project requirements and constraints
3. Technology and library selection
4. Design artifacts (use case diagram, UML)
5. Work division and timeline

**Discussion**

- **Symbology (EAN-13 vs Code 128):** The team debated both and chose **Code 128** by consensus vote, because it is less complex and only 6 digits are needed.
- **Requirements:** The team reviewed the requirements: generate 6-digit barcodes, read them from an uploaded image, and implement the camera feature as well. (R1 to R5)
- **Barcode generation:** Non-standard libraries cannot be used to generate barcodes, so the team will write the generation logic itself and pass the result to an image generator. Whether that generator is **Qt or OpenCV** is still to be decided.
- **Barcode reading:** **ZXing-cpp** will process the barcode image, extract the data, and return the result.
- **Front-end:** **Qt** will be used.
- **Design artifacts:** The use case diagram was discussed. The UML diagram will be made in the next meeting, once all members have produced their low-level designs, so the team can check for overlap and interdependencies.
- **Work division:** To be done after the meeting. (A1)
- **Timeline:** The coding phase starts from the next meeting (09/11/2026).

**Decisions**

- Use **Code 128** as the barcode format (consensus vote; less complexity, only 6 digits needed).
- Implement the barcode generation logic in-house, with no external barcode-generation library.
- Render the barcode image using **Qt or OpenCV**. **Open: to be decided.**
- Use **ZXing-cpp** to process barcode images and return the decoded data.
- Use **Qt** for the front-end.
- Implement camera scanning in addition to image upload.

**Action items raised:** A1 to A6 (see Action Items)

**Next meeting (09/11/2026) agenda**

1. Present each member's low-level design
2. Create the UML diagram; check overlap and interdependencies
3. Finalize Qt vs OpenCV
4. Confirm work division
5. Begin the coding phase

### Meeting 2: Interdependencies and Low-Level Designs (09/11/2026)

**Agenda**

1. Interdependencies between member work divisions
2. Low-level designs
3. Qt vs OpenCV (carried over from Meeting 1)
4. Internal deadlines

**Discussion**

- **Interdependencies:** The team discussed how the work divisions depend on each other (see [Work Division](#work-division)).
- **Low-level designs:** Members' low-level designs were discussed.
- **Image processing library:** Qt was finalized for image processing, resolving the open Qt vs OpenCV question from Meeting 1.
- **Internal deadlines:** Deadlines were set to make sure the parts come together before the demo (see Decisions).
- **Next meeting:** Members' code parts should be completed by then, and the meeting will focus on integration and testing. Writing test cases to check outputs was suggested.

**Decisions**

- Use **Qt** for image processing (resolves the open item from Meeting 1).
- Internal deadlines:
  - **10/11/2026 (night):** code finalization
  - **11/11/2026:** integration and testing
  - **12/11/2026:** graded demo

**Action items raised:** A7 to A9 (see Action Items). A1 to A3 closed.

**Next meeting (date to be confirmed) agenda**

1. Review the completed code parts from each member
2. Integrate the parts
3. Test the application
4. Optionally write test cases to check outputs

<!--

Template

### Meeting N: <Title> (DD/MM/YYYY)

**Agenda**
1.

**Discussion**
-

**Decisions**
-

**Action items raised:** A? (added to Action Items table)

**Next meeting (DD/MM/YYYY) agenda**
1.
-->
