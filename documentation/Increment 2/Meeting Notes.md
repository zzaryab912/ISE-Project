# Increment 2: Camera-Based GUI POS — Meeting Notes

| | |
|---|---|
| **Project** | Super store inventory and sales application (C++) |
| **Increment** | 2: Camera-Based GUI POS |
| **Graded demo** | 19/11/2026 (in class) |
| **Last updated** | 16/11/2026 (after Meeting 1) |

> **How to maintain this file:** After each meeting, (1) add a new section under [Meetings](#meetings), including that meeting's decisions, (2) update statuses and add new rows in the [Action Items](#action-items) table, (3) update the [Work Division](#work-division) table if responsibilities change, and (4) change "Last updated" above.

---

## Work Division

Responsibilities as assigned after Meeting 1.

| Member | Name | Area | Responsibilities |
|---|---|---|---|
| 1 | Zaryab | Interface | **Qt** interface, **CMake** and application integration |
| 2 | Nosher | Product Service | Product catalogue operations; stock rules; generating and saving product barcode images, using Increment 1 |
| 3 | Khurram | Storage | Reading and writing the **four text files**; generating product, order and bill IDs |
| 4 | Rayyan | Cart, pricing and scanning | Current cart and quantities; pricing calculator; continuous camera scanning |
| 5 | Rayan Dar | Checkout and History | Generating paid / held bills; recording payment; order history and archiving |

---

## Action Items

Running list. Keep completed items for the record; set their status to Done.

| # | Action | Owner | Due | Status | Raised in |
|---|---|---|---|---|---|
| A1 | **Revise the implementation plan** to correct the mistakes pointed out and include members' thoughts from Meeting 1 | Team | 16/11/2026 (mini-meeting) | Open | 1 |
| A2 | **Start the coding phase** | All members | After the 16/11/2026 mini-meeting | Open | 1 |
| A3 | **Document each member's own part of the code** (separate document, code comments, or a mix of both) | All members | During coding phase | Open | 1 |
| A4 | Push **completed functions** to the **"increment 2" branch**; keep incomplete code in **own separate branches** | All members | During coding phase | Open | 1 |

---

## Meetings

### Meeting 1: Work Division and Implementation Planning (15/11/2026)

**Agenda**

1. Division of tasks
2. Common data members and functions shared between members
3. Detailed implementation plan and rough program flow
4. User interface changes from Increment 1
5. SE principles in Increment 2
6. Documentation of each member's code
7. GitHub merging and branching
8. File handling
9. Next steps and mini-meeting

**Discussion**

- **Work division:** Tasks were divided into five areas: interface, product service, storage, cart/pricing/scanning, and checkout and history (see [Work Division](#work-division)).
- **Shared members and functions:** The common data members and functions to be used by multiple members were discussed so that interdependencies can be accounted for in the planning phase.
- **Implementation plan:** A detailed implementation plan was presented, covering each member's assigned work and the rough program flow.
- **User interface:** The UI is to be majorly revamped compared to Increment 1.
- **SE principles:** The SE principles, and how to incorporate them into Increment 2, were discussed in detail.
- **Documentation:** Special emphasis was placed on each member documenting their own part of the code, either in a separate document or in code comments. A mixture of both can be expected, as both are allowed.
- **GitHub merging:** Merging was discussed in depth, reflecting on the difficulties faced when merging in Increment 1, to improve integration in Increment 2.
- **File handling:** File handling intricacies were discussed, since most of the file handling falls under one member's area (Storage).
- **Next steps:** A mini-meeting is expected on 16/11/2026 to finalize the implementation plan, revised for the mistakes pointed out and the members' thoughts from this meeting.

**Decisions**

- Work is divided as recorded in the **Work Division** table.
- The **UI** will be **majorly revamped** compared to Increment 1.
- Each member **documents their own part** of the code, through a separate document, code comments, or a mix of both.
- **Completed functions** are pushed to the **"increment 2" branch**; **incomplete code** is kept in each member's **own separate branch**.
- Hold a **mini-meeting on 16/11/2026** to finalize the implementation plan. The **coding phase starts after** that meeting.

**Action items raised:** A1 to A4 (see Action Items).

**Next meeting (16/11/2026) agenda**

1. Review the final implementation plan, revised for the mistakes pointed out in Meeting 1
2. Incorporate members' thoughts on the plan
3. Start the coding phase

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
