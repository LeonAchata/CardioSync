#ifndef AWS_CONFIG_H
#define AWS_CONFIG_H

// ============================================================================
// CONFIGURACIÓN WIFI
// ============================================================================
#define WIFI_SSID "TuWiFi"
#define WIFI_PASSWORD "TuPassword"


// ============================================================================
// CONFIGURACIÓN AWS IOT CORE
// ============================================================================
#define AWS_IOT_ENDPOINT "xxxxxx-ats.iot.us-east-1.amazonaws.com"
#define DEVICE_ID "esp32-holter-001"
#define AWS_IOT_PORT 8883

// Topics MQTT
#define TOPIC_REQUEST "holter/upload-request"
#define TOPIC_RESPONSE "holter/upload-url/esp32-holter-001"

// ============================================================================
// CERTIFICADO ROOT CA (Amazon Root CA 1)
// ============================================================================
const char AWS_CERT_CA[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
-----END CERTIFICATE-----
)EOF";

// ============================================================================
// CERTIFICADO DEL DISPOSITIVO
// Copia el contenido de certificate.pem.crt aquí
// ============================================================================
const char AWS_CERT_CRT[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
-----END CERTIFICATE-----

)EOF";

// ============================================================================
// CLAVE PRIVADA DEL DISPOSITIVO
// Copia el contenido de private.pem.key aquí
// ============================================================================
const char AWS_CERT_PRIVATE[] PROGMEM = R"EOF(
-----BEGIN RSA PRIVATE KEY-----
-----END RSA PRIVATE KEY-----

)EOF";

#endif