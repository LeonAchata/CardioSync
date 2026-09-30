#ifndef HOLTER_CAPTURE_H
#define HOLTER_CAPTURE_H

#include <Arduino.h>
#include <XSpaceBioV10.h>

// ============================================================================
// PUBLIC INTERFACE
//
// Sampling runs in a dedicated FreeRTOS task paced with vTaskDelayUntil, so
// the ECG keeps a uniform sample period even while the main loop is blocked
// writing to the SD card. The main loop only drains the sample queue to disk.
// ============================================================================

/**
 * Initialises the capture module: AD8232 front-ends, SPI bus and SD card.
 * Must be called once from setup().
 */
void holter_init(XSpaceBioV10Board* bioBoard);

/**
 * Starts a new ECG capture session.
 * The file is named after the Unix time when the clock is synced, otherwise
 * after a boot-persistent counter stored in NVS.
 * @return true if the file was created and sampling started
 */
bool holter_startCapture();

/**
 * Drains captured samples to the SD card. Call continuously while capturing.
 * Finalises the file automatically once the configured duration is reached.
 */
void holter_captureLoop();

/**
 * Aborts the current capture (if any) and finalises the file.
 */
void holter_stopCapture();

/**
 * @return true while a capture is in progress
 */
bool holter_isCapturing();

/**
 * @return capture progress between 0.0 and 1.0
 */
float holter_getProgress();

/**
 * @return seconds elapsed since the current capture started
 */
unsigned long holter_getElapsedSeconds();

/**
 * @return full path of the last capture file (e.g. "/session_1727712000.bin")
 */
String holter_getCurrentFile();

/**
 * @return number of ECG samples written to the current/last file
 */
unsigned long holter_getECGSampleCount();

/**
 * @return samples lost because the SD writer could not keep up
 */
unsigned long holter_getDroppedSampleCount();

/**
 * @return true if the SD card was mounted successfully
 */
bool holter_isSDAvailable();

/**
 * Lists finished session files still stored on the SD card (not uploaded yet).
 * @param out   array that receives the full paths
 * @param max   capacity of the array
 * @return number of paths written to out
 */
size_t holter_listPendingFiles(String* out, size_t max);

#endif // HOLTER_CAPTURE_H
