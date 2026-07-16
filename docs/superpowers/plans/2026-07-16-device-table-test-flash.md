# Device Table Test Flash Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Keep discovered device data visible when selecting rows, make table widths adjustable, expose actions for the new `0x0A03` device, and add test firmware plus a visual progress simulation for the flash stub.

**Architecture:** Split the work into three focused areas. `MainWindow` owns table presentation, selection, column sizing, and action rendering; `device-catalog.json` defines the new device metadata and capability set; `workflow.cpp` simulates flash progress and emits incremental log messages so the UI can show activity without hardware access.

**Tech Stack:** Qt Widgets, Qt Network, Qt SerialPort, qmake, C++17.

---

### Task 1: Make the device table stable and resizable

**Files:**
- Modify: `app/src/main_window.cpp`
- Modify: `app/src/main_window.h`

- [ ] **Step 1: Confirm the current failure path**

Check `MainWindow::addDeviceRow`, `MainWindow::onDeviceFound`, and `MainWindow::rebuildBulkMenu` so row selection does not rebuild or clear existing row widgets.

- [ ] **Step 2: Implement the UI fix**

Keep existing device rows intact when the user clicks a row, make the checkboxes live in a stable item model, and configure `QHeaderView` sections to be user-resizable with sensible minimum widths.

- [ ] **Step 3: Verify behavior in build**

Run the Windows Qt build and confirm the table still shows `device`, `channel`, `type`, `version`, `description`, `status`, and `actions` after row selection.

### Task 2: Add test metadata and firmware for `0x0A03`

**Files:**
- Modify: `app/config/device-catalog.json`
- Add: `app/flash/boc-v12/application-test.bin`
- Add: `app/flash/boc-v12/bootloader-test.bin`

- [ ] **Step 1: Add catalog entry**

Add a `deviceTypes` entry with:

```json
{
  "id": "boc.v12",
  "protocol": "unicorn-ascii",
  "type": "0x0A03",
  "name": "БОЦ-В-12",
  "description": "Блок обработки цифровой (БОЦ-В-12)",
  "expectedDescription": "Блок обработки цифровой (БОЦ-В-12)",
  "capabilities": [
    "identity.read",
    "flash.application.write",
    "flash.bootloader.write"
  ],
  "flashWorkflows": {
    "application": "flash.test",
    "bootloader": "flash.test"
  },
  "firmwareFiles": [
    {
      "target": "application",
      "version": "test-1.0.0",
      "relativePath": "flash/boc-v12/application-test.bin",
      "sha256": "calculated-after-file-creation"
    },
    {
      "target": "bootloader",
      "version": "test-1.0.0",
      "relativePath": "flash/boc-v12/bootloader-test.bin",
      "sha256": "calculated-after-file-creation"
    }
  ],
  "deviceClass": "DeviceBase"
}
```

- [ ] **Step 2: Create the firmware files**

Write small deterministic binary files so `WorkflowRunner::sha256File` can resolve them and report a valid hash during the test flow.

- [ ] **Step 3: Update hashes**

Compute the SHA-256 values for both files and store them in `device-catalog.json`.

### Task 3: Add visual flash progress to the stub workflow

**Files:**
- Modify: `app/src/workflow.h`
- Modify: `app/src/workflow.cpp`

- [ ] **Step 1: Add progress-oriented log output**

Extend `WorkflowRunner::run` so each device emits stepwise messages such as queued, validating artifact, erasing, writing, verifying, and completed.

- [ ] **Step 2: Keep it hardware-free**

Continue to avoid real flash transport calls, but make the stub look like a real operation by simulating progress in code and emitting intermediate percentage updates in the log.

- [ ] **Step 3: Verify the action is selectable**

Confirm the `0x0A03` device now exposes at least two actions in the row menu and the bulk menu when checked.

### Task 4: Verify end-to-end

**Files:**
- No new files

- [ ] **Step 1: Build the project**

Run the existing Windows Qt MinGW build and confirm there are no compilation errors.

- [ ] **Step 2: Smoke test the UI**

Launch the app, select a discovered device row, confirm the data remains visible, adjust a few column widths, and trigger the test flash action to see progress logs.

