# ECG-IMU XSpace - Sistema Holter IoT con AWS

Sistema de monitoreo Holter portátil basado en ESP32 que captura señales ECG de 3 derivaciones y datos de acelerómetro IMU, almacenándolos en formato binario y subiéndolos automáticamente a AWS S3 mediante AWS IoT Core.

## 📋 Características

- **Captura de ECG**: 3 derivaciones (I, II, III) a 100 Hz
- **Sensor IMU**: Acelerómetro ADXL345 (3 ejes) a 100 Hz
- **Almacenamiento local**: SD Card con formato binario optimizado (int16)
- **Conectividad IoT**: AWS IoT Core mediante MQTT sobre TLS
- **Upload automático**: URLs presignadas de S3 via Lambda
- **Modo prueba**: Testing sin hardware para validar comunicación AWS
- **Bajo consumo**: WiFi desactivado durante captura

## 🔧 Hardware

- **Placa**: XSpace Bio V1.0 (ESP32)
- **ECG**: 2x AD8232 (derivaciones I y II, III calculada)
- **IMU**: ADXL345 (I2C, opcional)
- **Almacenamiento**: MicroSD Card (SPI, opcional para pruebas)
- **WiFi**: 2.4 GHz integrado en ESP32

### Conexiones

```
ESP32 Pin 5  → SD Card CS
I2C SDA/SCL  → ADXL345
AD8232_XS1   → Derivación I
AD8232_XS2   → Derivación II
```

## 📦 Dependencias

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino

lib_deps = 
    adafruit/Adafruit ADXL345@^1.3.4
    knolleary/PubSubClient@^2.8
    bblanchon/ArduinoJson@^6.21.5
```

## 🔐 Configuración AWS

### 1. AWS IoT Core

Crear un **Thing** en AWS IoT Core:

```bash
# Descargar certificados:
- Root CA (Amazon Root CA 1)
- Device Certificate
- Private Key
```

### 2. Configurar `include/aws_config.h`

```cpp
#define WIFI_SSID "TuWiFi"
#define WIFI_PASSWORD "TuPassword"

#define AWS_IOT_ENDPOINT "xxxxxx-ats.iot.us-east-1.amazonaws.com"
#define DEVICE_ID "esp32-holter-001"

#define TOPIC_REQUEST "holter/upload-request"
#define TOPIC_RESPONSE "holter/upload-url/esp32-holter-001"

// Pegar certificados descargados
const char AWS_CERT_CA[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
...
-----END CERTIFICATE-----
)EOF";

const char AWS_CERT_CRT[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
...
-----END CERTIFICATE-----
)EOF";

const char AWS_CERT_PRIVATE[] PROGMEM = R"EOF(
-----BEGIN RSA PRIVATE KEY-----
...
-----END RSA PRIVATE KEY-----
)EOF";
```

### 3. IoT Rule

Crear regla en AWS IoT Core para triggear Lambda:

```json
{
  "sql": "SELECT * FROM 'holter/upload-request'",
  "actions": [{
    "lambda": {
      "functionArn": "arn:aws:lambda:REGION:ACCOUNT:function:GenerateUploadURL"
    }
  }]
}
```

### 4. Lambda Function

La Lambda debe:
1. Recibir mensaje del ESP32 con metadata
2. Generar URL presignada de S3 (PUT)
3. Publicar respuesta via MQTT al topic `holter/upload-url/{device_id}`

```python
import boto3
import json

s3_client = boto3.client('s3')
iot_client = boto3.client('iot-data')

def lambda_handler(event, context):
    device_id = event['device_id']
    session_id = event['session_id']
    
    # Generar URL presignada
    url = s3_client.generate_presigned_url(
        'put_object',
        Params={
            'Bucket': 'holter-raw-data',
            'Key': f'raw/{device_id}/{session_id}.bin',
            'ContentType': 'application/octet-stream'
        },
        ExpiresIn=3600
    )
    
    # Responder via MQTT
    iot_client.publish(
        topic=f'holter/upload-url/{device_id}',
        qos=1,
        payload=json.dumps({
            'status': 'success',
            'upload_url': url
        })
    )
```

### 5. Permisos IAM

Lambda execution role necesita:

```json
{
  "Version": "2012-10-17",
  "Statement": [
    {
      "Effect": "Allow",
      "Action": "s3:PutObject",
      "Resource": "arn:aws:s3:::holter-raw-data/*"
    },
    {
      "Effect": "Allow",
      "Action": "iot:Publish",
      "Resource": "arn:aws:iot:REGION:ACCOUNT:topic/holter/upload-url/*"
    }
  ]
}
```

## 🚀 Uso

### Compilar y Subir

```bash
platformio run -t upload
platformio device monitor
```

### Flujo de Operación

1. **Inicio**: ESP32 arranca y verifica hardware
2. **Captura**: 
   - Con SD: Captura 10 segundos de ECG/IMU
   - Sin SD: Modo prueba (solo comunicación AWS)
3. **Conexión WiFi**: Se conecta después de la captura
4. **MQTT**: 
   - Conecta a AWS IoT Core
   - Publica solicitud al topic `holter/upload-request`
5. **Lambda**: Genera URL presignada
6. **Upload**: 
   - Con SD: Sube archivo binario a S3
   - Sin SD: Completa flujo de prueba
7. **Reinicio**: Ciclo cada 10 segundos

### Configuración de Duración

Modificar en `src/main.cpp`:

```cpp
const int CAPTURE_DURATION_SEC = 10;  // Cambiar a 30, 60, 1800, etc.
```

## 📊 Formato de Datos

### Archivo Binario (`.bin`)

```
[Header 32 bytes]
[ECG Sample 1: 6 bytes]
[IMU Sample 1: 12 bytes]
[ECG Sample 2: 6 bytes]
[IMU Sample 2: 12 bytes]
...
```

#### Header (32 bytes)

```c
struct FileHeader {
  uint32_t magic;              // 0x45434744 = "ECGD"
  uint16_t version;            // 1
  uint16_t device_id;          // ID del dispositivo
  uint32_t session_id;         // Timestamp Unix
  uint32_t timestamp_start;    // Timestamp Unix inicio
  uint16_t ecg_sample_rate;    // 100 Hz
  uint16_t imu_sample_rate;    // 100 Hz
  uint32_t num_ecg_samples;    // Total muestras ECG
  uint32_t num_imu_samples;    // Total muestras IMU
  uint8_t reserved[4];         // Reservado
} __attribute__((packed));
```

#### ECG Sample (6 bytes)

```c
struct ECGSample {
  int16_t derivation_I;    // Escalado: ±5mV → ±32768
  int16_t derivation_II;   // Factor: 6553.6
  int16_t derivation_III;  // III = II - I
} __attribute__((packed));
```

#### IMU Sample (12 bytes)

```c
struct IMUSample {
  int16_t accel_x;  // Aceleración X
  int16_t accel_y;  // Aceleración Y
  int16_t accel_z;  // Aceleración Z
  int16_t gyro_x;   // Giroscopio X (no implementado)
  int16_t gyro_y;   // Giroscopio Y (no implementado)
  int16_t gyro_z;   // Giroscopio Z (no implementado)
} __attribute__((packed));
```

### Conversión a Voltaje

```python
# Convertir int16 a mV
voltage_mV = int16_value / 6553.6
```

### Tamaño de Archivo

Para captura de 10 segundos:
- ECG: 1000 samples × 6 bytes = 6 KB
- IMU: 1000 samples × 12 bytes = 12 KB
- Header: 32 bytes
- **Total**: ~18 KB

Para 30 minutos (1800s):
- **Total**: ~3.2 MB

## 🔍 Debugging

### Logs del Sistema

```
[OK]      - Operación exitosa
[INFO]    - Información general
[WARNING] - Advertencia (no crítico)
[ERROR]   - Error (puede continuar)
[DEBUG]   - Información de depuración
```

### Errores Comunes

#### 1. MQTT Connection Lost (-3)

```
[DEBUG] MQTT Estado: -3
[DEBUG] MQTT Conectado: NO
```

**Solución**: 
- Verificar certificados en `aws_config.h`
- Verificar endpoint de AWS IoT
- Verificar política de IoT (iot:Connect, iot:Publish, iot:Subscribe)

#### 2. Timeout Esperando URL

```
[ERROR] Timeout esperando URL (60s)
```

**Solución**:
- Verificar que IoT Rule esté habilitada
- Verificar que Lambda tenga permisos `iot:Publish`
- Revisar logs de Lambda en CloudWatch

#### 3. No Se Pudo Publicar

```
[ERROR] No se pudo publicar
```

**Solución**:
- Aumentar `mqttClient.setBufferSize(4096)`
- Verificar que el topic coincida con la IoT Rule
- Verificar keepAlive y llamar `mqttClient.loop()`

## 📈 Monitoreo AWS

### MQTT Test Client

En AWS IoT Console → Test:

```
# Subscribe para ver mensajes del ESP32
holter/upload-request

# Subscribe para ver respuestas de Lambda
holter/upload-url/#
```

### CloudWatch Logs

```
/aws/lambda/GenerateUploadURL
```

### S3 Bucket

Estructura:
```
holter-raw-data/
└── raw/
    └── YYYY/
        └── MM/
            └── DD/
                └── esp32-holter-001/
                    └── session_1234567890.bin
```

## 🛠️ Desarrollo

### Modo Prueba (Sin Hardware)

El sistema detecta automáticamente hardware faltante:

- **Sin IMU**: Usa valores 0 para acelerómetro
- **Sin SD**: Salta captura, solo prueba comunicación AWS

Útil para:
- Testing de conectividad AWS
- Desarrollo sin hardware completo
- Validación de integración Lambda

### Parámetros Configurables

```cpp
// Duración de captura
const int CAPTURE_DURATION_SEC = 10;

// Frecuencias de muestreo
const int ECG_SAMPLE_RATE_HZ = 100;
const int IMU_SAMPLE_RATE_HZ = 100;

// Escalado ECG (mV → int16)
const float ECG_SCALE_FACTOR = 6553.6;

// Buffer de escritura SD
const int BUFFER_SIZE = 512;

// Timeout upload
const unsigned long UPLOAD_TIMEOUT_MS = 30000;
```

## 📝 TODO / Mejoras Futuras

- [ ] Compresión GZIP de archivos antes de upload
- [ ] Detección de QRS en tiempo real
- [ ] Modo de bajo consumo (deep sleep entre capturas)
- [ ] Sincronización NTP para timestamps precisos
- [ ] OTA (Over-The-Air) updates via AWS
- [ ] Dashboard web para visualización en tiempo real
- [ ] Almacenamiento en DynamoDB de metadata
- [ ] Procesamiento Lambda para extracción de características

## 📄 Licencia

Este proyecto fue desarrollado para el curso de Instrumentación en PUCP.

## 👥 Autor

Desarrollado como parte del proyecto Holter IoT - PUCP 2025

## 🔗 Referencias

- [ESP32 Arduino Core](https://github.com/espressif/arduino-esp32)
- [AWS IoT Core Documentation](https://docs.aws.amazon.com/iot/)
- [PubSubClient MQTT Library](https://github.com/knolleary/pubsubclient)
- [XSpace Bio Board](https://github.com/XSpaceTech)
