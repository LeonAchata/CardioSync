#ifndef AWS_CONFIG_H
#define AWS_CONFIG_H

// ============================================================================
// AWS IoT CORE
//
// This file is generated automatically by `terraform apply` (infra/terraform).
// To configure it by hand instead, copy it to aws_config.h (git-ignored) and
// paste the certificates of your IoT thing.
// ============================================================================

#define AWS_IOT_ENDPOINT "xxxxxxxxxxxxxx-ats.iot.us-east-1.amazonaws.com"
#define AWS_IOT_PORT 8883

// Must match the IoT thing name (the IoT policy only allows this client ID)
#define DEVICE_ID "cardiosync-esp32-001"

// MQTT topics
#define TOPIC_REQUEST "cardiosync/upload-request"
#define TOPIC_RESPONSE "cardiosync/upload-url/" DEVICE_ID

// Amazon Root CA 1 - https://www.amazontrust.com/repository/AmazonRootCA1.pem
const char AWS_CERT_CA[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
-----END CERTIFICATE-----
)EOF";

// Device certificate (certificate.pem.crt)
const char AWS_CERT_CRT[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
-----END CERTIFICATE-----
)EOF";

// Device private key (private.pem.key)
const char AWS_CERT_PRIVATE[] PROGMEM = R"EOF(
-----BEGIN RSA PRIVATE KEY-----
-----END RSA PRIVATE KEY-----
)EOF";

// Optional: root CA used to verify the S3 endpoint during the HTTPS upload.
// When not defined the upload is encrypted but the server is not verified.
// #define S3_ROOT_CA AWS_CERT_CA

#endif // AWS_CONFIG_H
