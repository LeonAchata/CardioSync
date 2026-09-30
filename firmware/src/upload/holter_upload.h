#ifndef HOLTER_UPLOAD_H
#define HOLTER_UPLOAD_H

#include <Arduino.h>

// ============================================================================
// UPLOAD STATES
// ============================================================================

enum UploadState {
  UPLOAD_IDLE,
  UPLOAD_CONNECTING_WIFI,
  UPLOAD_CONNECTING_MQTT,
  UPLOAD_REQUESTING_URL,
  UPLOAD_UPLOADING_S3,
  UPLOAD_COMPLETE,
  UPLOAD_ERROR
};

// ============================================================================
// PUBLIC INTERFACE
//
// Flow: WiFi -> MQTT (AWS IoT Core, mutual TLS) -> publish upload request ->
// receive S3 presigned URL -> HTTPS PUT streamed from the SD card.
// ============================================================================

/**
 * Loads the AWS IoT certificates. Must be called once from setup().
 */
void holter_initUpload();

/**
 * Starts uploading a session file. WiFi/MQTT are reused if already connected.
 * @param filename full SD path of the file (e.g. "/session_1727712000.bin")
 * @return true if the upload was started
 */
bool holter_startUpload(const String& filename);

/**
 * Advances the upload state machine. Call continuously while uploading.
 */
void holter_uploadLoop();

/**
 * Cancels the current upload and closes the MQTT session.
 */
void holter_cancelUpload();

/**
 * Closes the MQTT session (call before turning WiFi off).
 */
void holter_disconnectMQTT();

/**
 * @return true while an upload is in progress
 */
bool holter_isUploading();

/**
 * @return coarse progress between 0.0 and 1.0
 */
float holter_getUploadProgress();

/**
 * @return current state of the upload state machine
 */
UploadState holter_getUploadState();

/**
 * @return human-readable description of the current state
 */
String holter_getUploadStateString();

/**
 * @return true if the MQTT session is up
 */
bool holter_isMQTTConnected();

/**
 * @return last error message ("" if none)
 */
String holter_getLastError();

#endif // HOLTER_UPLOAD_H
