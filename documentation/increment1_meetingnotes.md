# Increment 1: Bar Code Generator and Reader — Meeting Notes

| | |
|---|---|
| **Project** | Super store inventory and sales application (C++) |
| **Increment** | 1: Bar Code Generator and Reader |
| **Graded demo** | 12/11/2026 |
| **Last updated** | 08/11/2026 (after Meeting 1) |

> **How to maintain this file:** After each meeting, (1) add a new section under [Meetings](#meetings), including that meeting's decisions, (2) update statuses and add new rows in the [Action Items](#action-items) table, and (3) change "Last updated" above.

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

## Action Items

Running list. Keep completed items for the record; set their status to Done.

| # | Action | Owner | Due | Status | Raised in |
|---|---|---|---|---|---|
| A1 | Divide work among team members | Team | After Meeting 1 | Open | 1 |
| A2 | Prepare individual **low-level designs** | All members | Before 09/11/2026 | Open | 1 |
| A3 | Decide between **Qt and OpenCV** for image generation | Team | Meeting 2 | Open | 1 |
| A4 | Create the **UML diagram**; review for overlap and interdependencies | Team | Meeting 2 | Open | 1 |
| A5 | Begin the **coding phase** | Team | From 09/11/2026 | Open | 1 |
| A6 | Have a working Increment 1 ready for the **graded demo** | Team | 12/11/2026 | Open | 1 |

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